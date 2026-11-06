-- W4 alignment against the real outcome/phase apply rules, recording APIs.
-- Usage: lua51 this.lua world_align.lua world_enemy.lua world_outcome.lua world_phase.lua world_recovery.lua world_probe.lua
--        [world_place.lua world_place_guest.lua] (spawn sync, docs/debug-menu.md)
local checks = 0
local function check(ok, why) checks = checks + 1; if not ok then error(checks .. ': ' .. why, 2) end end
local OP = {RESYNC = 5, RESYNC_END = 6, OUTCOME = 16, REQ_OUTCOME = 17, PHASE = 18, REQ_PHASE = 19,
    AREA = 20, PVEC = 23, ALIGN_BEGIN = 14, ALIGN_END = 15, ALIGN_RECORD = 24, ALIGN_ACK = 25, REJECT = 4, RECEIVED = 3,
    GAME_OVER = 26, REQ_GAME_OVER = 27, CHECKPOINT = 28, PROBE = 116, PROBE_FIRE = 117, PROBE_SYNC = 8,
    PAIRED = 113, UNPAIRED = 114}
local function fixture(host, level, mb)
    local c = {now = 1000, tx = {}, logs = {}, commands = {}, life = {}, action = {}, phase = {cp_a = 10, cp_b = 10},
        positions = {}, receipts = {}, warp = {}, enemyCalls = {}, setOut = {}}
    local env = setmetatable({e = {}, vars = {playerPosX = 12.25, playerPosY = 30.1, playerPosZ = -8.5, playerRotY = -45.125},
        mvars = {mis_checkPointList = {cp_start = 1}}, svars = {}, gvars = {mis_checkPoint = 444},
        TppDefine = {GAME_OVER_TYPE = {PLAYER_DEAD = 1, PLAYER_FALL_DEAD = 2}, GAME_OVER_RADIO = {PLAYER_DEAD = 1}},
        TppMission = {}, TppCheckPoint = {}, TppPlayer = {}}, {__index = _G})
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {}, bindHooks = {},
        active = true, level = level or 3, host = host, role = host and 1 or 2, epoch = 7,
        feature = mb and {enemies = true, allCp = true, patrol = false, spareStaff = true} or {enemies = true},
        paired = true, eff = level or 3, mode = 1, mission = mb and 30050 or 30010, roster = 4000000000, rosterOk = true,
        w1ok = true, w1 = true,
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
            if command.id == 'GetStatus' and c.status then return c.status[id] end
            if command.id == 'GetGameObjectIdUsingRoute' then return c.routeIds and c.routeIds[command.route] end
            if command.id == 'IsOnRoute' then return c.onRoute ~= nil and c.onRoute[id] == command.route end
            if command.id == 'GetVehicleGameObjectId' and c.vehicles then return c.vehicles[id] or 65535 end
            c.commands[#c.commands + 1] = {id, command.id, command.state or command.phase, command.forceRealize, command.route}
            if command.id == 'ChangeLifeState' then if not c.inert then c.life[id] = command.state end
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
        -- FM1 (world_mirror.lua): ACT_V1 receipts go to c.acts only (the other checks read c.receipts in order).
        if source:find("'MGS_COOP_WORLD_ACT_V1'", 1, true) then
            c.acts = c.acts or {}
            c.acts[#c.acts + 1] = source
            if source:find("-- p ", 1, true) then return c.pReply or {p = 1} end
            return c.actReply or {f = 1, sh = c.shots or 0}
        end
        c.receipts[#c.receipts + 1] = source
        if source:find("'MGS_COOP_WORLD_WARP_V1'", 1, true) or source:find("'MGS_COOP_WORLD_WARP_HOST_V1'", 1, true) then
            local r = #c.warp > 1 and table.remove(c.warp, 1) or c.warp[1]
            if r == 'literal' then return source:match("^return '([^']+)'") end
            return r
        end
        if source:find("'MGS_COOP_DEBUG_REMOTE_V1'", 1, true) then return true end
        if source:find("'MGS_COOP_WORLD_DRIVE_V1'", 1, true) then
            c.drives = c.drives or {}
            c.drives[#c.drives + 1] = source
            return c.driveReply or {e = 120, s = 1, r = 0, h = 1}
        end
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
    for _, path in ipairs({arg[2], arg[3], arg[4], arg[6], arg[1], arg[5], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12]}) do
        local chunk = assert(loadfile(path)); setfenv(chunk, env); chunk()
    end
    W.align.install({level or 3})
    if W.recovery then W.recovery.install({level or 3}) end
    for _, f in ipairs(W.startHooks) do f() end
    -- Mother Base has no free-roam area trap: W.activeCp stays nil there.
    if not mb then W.activeCp = {[100] = true, [200] = not host} end
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
        c.env.vars.missionCode = 30010
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
    -- Enemy latch (run e583ee62): out=0 handled=0 from the typed soldier dispatch (native "relayed": the guard is not
    -- realized on this copy) and the native backoff that follows it are why=unrealized (23), never native_other.
    check(refusal(function(gg) gg.warp = {{refused = 'relayed'}} end, 'unrealized'), 'relayed: not realized on this copy')
    check(refusal(function(gg) gg.warp = {{refused = 'backoff'}} end, 'unrealized'), 'backoff: that guard waits natively')
    check(placeFixture(false).P.WHY[23] == 'unrealized', 'WHY 23 = unrealized (appended: older codes unchanged)')
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
    -- W1 off keeps spawn sync; a stop releases the pending force-realize and forgets everything.
    local hs, gs = placeFixture(true), placeFixture(false)
    gs.warp = {{r = -1, h = 0}}
    hs.P.share(1, ENT1, 1000, 0, 1000, 0, 2); hs.P.share(5, ENT1)
    deliver(hs, gs, 34); run(gs, 2)
    local placeStop = gs.W.stopHooks[#gs.W.stopHooks]
    placeStop(false)
    check(gs.P.cur and #gs.P.q == 1 and gs.W.pinned[ENT1] and sent(gs, 1, 'SetForceRealize', false) == 0,
        'W1 off (stop hook false) keeps the pending placement, queue and pins')
    placeStop(true)
    check(gs.P.cur == nil and #gs.P.q == 0 and next(gs.W.pinned) == nil and sent(gs, 1, 'SetForceRealize', false) == 1,
        'a stop releases force-realize and clears the queue and pins')
    local ht, gt = placeFixture(true), placeFixture(false)
    gt.warp = {{r = -1, h = 0}}
    ht.P.share(1, ENT1, 1000, 0, 1000, 0, 2)
    deliver(ht, gt, 34); run(gt, 2)
    gt.inGame = false
    gt.W.stopHooks[#gt.W.stopHooks](true)
    check(gt.P.cur == nil and sent(gt, 1, 'SetForceRealize', false) == 0, 'teardown (not in gameplay): no game call, state forgotten')
    hs.W.active = false
    check(hs.P.share(5, ENT1) == 'sync=none;why=world_off', 'an inactive world link reports world_off, not guest')
    -- Corrections are not sampled while the guard's placement is pending; a queued one takes the newest target.
    local hp, gp = placeFixture(true), placeFixture(false)
    gp.warp = {{r = -1, h = 0}}
    hp.positions[1] = {1.0, 0, 1.0}
    hp.P.share(1, ENT1, 1000, 0, 1000, 0, 2)
    deliver(hp, gp, 34); run(gp, 1)
    gp.positions[1] = {90.0, 0, 90.0}
    for i = 1, 3 do
        hp.positions[1] = {1.0 + 2 * i, 0, 1.0}
        hp.now = hp.now + 1000; hp.P.tick(hp.now)
        deliver(hp, gp, 34)
    end
    check(gp.P.cur and gp.P.cur.kind == 1 and #gp.P.q == 0 and gp.P.div[ENT1] == nil, 'no divergence samples while the warp is pending')
    gp.P.cur, gp.P.q = {h = 3000000002, kind = 5, seq = 99, flags = 0, at = gp.now, frames = 0}, {}
    gp.P.div[ENT1] = {n = 1, fixes = 0, at = -1e9}
    hp.positions[1] = {12.0, 0, 1.0}; hp.now = hp.now + 1000; hp.P.tick(hp.now); deliver(hp, gp, 34)
    local queued = gp.P.q[1]
    check(#gp.P.q == 1 and queued.kind == 3 and queued.x == 12000, 'two samples over 6 m queue a correction behind another entry')
    hp.positions[1] = {14.0, 0, 1.0}; hp.now = hp.now + 1000; hp.P.tick(hp.now); deliver(hp, gp, 34)
    check(#gp.P.q == 1 and gp.P.q[1] == queued and queued.x == 14000 and queued.seq == hp.P.seq and queued.at == gp.now,
        'a later correction refreshes the queued one in place (newest seq, target and grant)')
    -- Disabled copies: a warp refuses, a route placement re-enables him (the host's own sequence).
    check(refusal(function(gg) gg.env.TppGameObject = {NPC_STATE_DISABLE = 9}; gg.status = {[1] = 9} end, 'disabled_local'),
        'a disabled copy is never warped')
    local hr, gr = placeFixture(true), placeFixture(false)
    gr.env.TppGameObject = {NPC_STATE_DISABLE = 9}; gr.status = {[1] = 9}
    gr.env.mvars.ene_routeSetsDefine = {cp_a = {sneak_day = {{'rt_a'}}}}
    hr.P.share(2, ENT1, 0, 0, 0, 0, 0, 'rt_a')
    deliver(hr, gr, 34); run(gr, 4)
    local routeAck = placeTx(gr, 35)[1]
    check(routeAck and routeAck.v[3] == 0 and gr.enemyCalls[1] == 'SetDisable:1' and gr.enemyCalls[2] == 'SetEnable:1',
        'a route placement of a disabled copy runs the retail sequence (re-enabled), as on the host')
    -- A "come" warp of a guard the guest ignores turns noticing back on, as on the host.
    local hc, gc = placeFixture(true), placeFixture(false)
    gc.warp = {{r = 0, h = 1}}
    gc.env.e.MgsCoopDebug.enemy.ignored.sol_1 = true
    hc.P.share(1, ENT1, 1000, 0, 1000, 0, 1)
    deliver(hc, gc, 34); run(gc, 4)
    check(gc.enemyCalls[1] == 'SetDisableRestrictNotice:1' and not gc.env.e.MgsCoopDebug.enemy.ignored.sol_1 and
        sent(gc, 1, 'SetDiscoveryPlayer') == 1, 'come warp: restrict notice off, then discovery')
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
    -- Continuous authored-soldier patrol & combat sync (W.w1 = true):
    local ph, pg = placeFixture(true), placeFixture(false)
    ph.W.w1, pg.W.w1 = true, true
    ph.W.activeCp = {[100] = true}
    ph.env.vars.playerPosX, ph.env.vars.playerPosZ = 0, 0
    ph.positions[1] = {10.0, 1.8, 12.0}
    ph.positions[2] = {80.0, 1.8, 80.0} -- beyond 55 m range: ignored
    for i = 3, 5 do ph.positions[i] = {90.0, 0, 90.0} end
    ph.now = ph.now + 600; ph.P.tick(ph.now)
    ph.now = ph.now + 600; ph.P.tick(ph.now)
    local ptx = placeTx(ph, 34)
    -- y: the feet (GetPosition y - 800 mm: run cb8ae74f's grounded copies landed 0.80 m over the request) plus the 400 mm
    -- lift (P.LIFT: a copy placed with its feet at the host's sank under the ground in runs 51f8e90f / 6e22bed0 /
    -- cb8ae74f, landing at 0.0 m and falling ~20 m in 2 s).
    check(#ptx == 1 and ptx[1].v[2] == ENT1 and ptx[1].v[3] == 3 and ptx[1].v[4] == 10000 and ptx[1].v[5] == 1400 and
        ptx[1].v[6] == 12000 and ptx[1].v[8] == 0, 'patrol scan announces nearby active-CP soldier with y - 800 + 400 mm')
    pg.positions[1] = {10.0, 1.8, 12.0}
    deliver(ph, pg, 34)
    check(#pg.P.q == 0, 'aligned guest soldier queues no correction')
    -- Soldier moves >= 1.5 m and CP enters ALERT -> flags = 1, guest triggers SetDiscoveryPlayer and queues warp when > 8 m off.
    ph.W.cp[100].localPhase = 3
    ph.positions[1] = {20.0, 1.8, 12.0}
    ph.now = ph.now + 600; ph.P.tick(ph.now); ph.P.tick(ph.now); ph.P.tick(ph.now)
    ptx = placeTx(ph, 34)
    check(#ptx == 1 and ptx[1].v[4] == 20000 and ptx[1].v[7] == 1571 and ptx[1].v[8] == 1,
        'patrol movement >= 1.5 m in ALERT sends heading yaw and combat flag 1')
    pg.warp = {{r = 0, h = 1, leaf = 'patrol'}}
    deliver(ph, pg, 34)
    -- The 0.14.7 combat nudge (SetDiscoveryPlayer on this copy while the host copy fights) pulled the copies apart
    -- (fb1101): gone, the line stays.
    check(#pg.P.q == 1 and pg.P.q[1].kind == 3 and sent(pg, 1, 'SetDiscoveryPlayer') == 0 and
        logged(pg, 'event=place_combat;seq=') and logged(pg, ';nudge=0'), 'combat sample: a correction queued on > 8 m divergence, no nudge')
    pg.positions[1] = {20.0, 1.8, 12.0}
    run(pg, 4)
    check(pg.P.cur == nil and sent(pg, 1, 'SetDiscoveryPlayer') == 0, 'a completed combat correction sends no SetDiscoveryPlayer')
    -- Mother Base (feature patrol = false): no patrol position corrections until a run shows them working there.
    local mbp = placeFixture(true)
    mbp.W.w1, mbp.W.feature, mbp.W.activeCp = true, {enemies = true, allCp = true, patrol = false}, nil
    mbp.env.vars.playerPosX, mbp.env.vars.playerPosZ = 0, 0
    mbp.positions[1] = {10.0, 1.8, 12.0}
    mbp.now = mbp.now + 600; mbp.P.tick(mbp.now)
    mbp.now = mbp.now + 600; mbp.P.tick(mbp.now)
    check(#placeTx(mbp, 34) == 0, 'Mother Base: no patrol position corrections (feature patrol = false)')
    -- Mother Base staff placement (feature staffPlace, mb-staff-sync): the host shares the standing position of the
    -- staff near EITHER player (the partner body, TppPlayer2 index 1), never a combat flag (no SetDiscoveryPlayer on
    -- staff, even in ALERT), at most 2 reads per tick.
    local MB = {enemies = true, allCp = true, spareStaff = true, staffPlace = true}
    local function staffFixture(host)
        local c = placeFixture(host)
        c.W.w1, c.W.feature, c.W.activeCp = true, MB, nil
        c.env.vars.playerPosX, c.env.vars.playerPosZ = 0, 0
        c.env.GameObject = {GetGameObjectIdByIndex = function(kind, index)
            c.partnerLookups = (c.partnerLookups or 0) + 1
            return kind == 'TppPlayer2' and index == 1 and 900 or nil
        end}
        return c
    end
    local function runE(c, frames)
        for _ = 1, frames do c.now = c.now + 33; c.E.tick(c.now); c.P.tick(c.now) end
    end
    local sh, sg = staffFixture(true), staffFixture(false)
    sh.positions[1] = {10.0, 1.8, 12.0}     -- near the host
    sh.positions[2] = {100.0, 1.8, 100.0}   -- near the partner only
    for i = 3, 5 do sh.positions[i] = {300.0, 0, 300.0} end
    sh.positions[900] = {98.0, 1.8, 97.0}   -- the partner body on the host
    sh.W.cp[100].localPhase = 3
    sh.P.patrol.combat[ENT1] = sh.now
    for _ = 1, 4 do sh.now = sh.now + 600; sh.P.tick(sh.now) end
    local stx = placeTx(sh, 34)
    check(#stx == 2 and stx[1].v[2] == ENT1 and stx[1].v[3] == 3 and stx[1].v[8] == 0 and stx[2].v[2] == 3000000002 and
        stx[2].v[4] == 100000 and stx[2].v[8] == 0, 'Mother Base: staff near either player shared (kind 3), flags 0 in ALERT')
    check(sh.partnerLookups ~= nil and sh.partnerLookups <= 4, 'partner position read at most once per tick')
    -- Place first: the host's own knockout of a standing staff member announces where they are (kind 3) BEFORE the
    -- OUTCOME, on the same reliable channel.
    -- The lying body reads lower (1.0 m) and the scanner may read it asleep first: the standing ground y is kept.
    sh.positions[1] = {10.6, 1.0, 12.0}
    sh.life[1] = 3
    for _ = 1, 3 do sh.now = sh.now + 600; sh.P.tick(sh.now) end
    sh.tx = {}
    sh.E.check(ENT1, 1)
    check(#sh.tx == 2 and sh.tx[1].op == 34 and sh.tx[1].v[2] == ENT1 and sh.tx[1].v[3] == 3 and sh.tx[1].v[4] == 10600 and
        sh.tx[1].v[5] == 1400 and sh.tx[2].op == 16 and sh.tx[2].v[3] == 2 and logged(sh, 'event=place_first;'),
        'host commit of a standing staff member: kind 3 with the standing ground y (+ the lift) first, then the OUTCOME')
    -- A staff member never sampled standing (far from both players) gets no place-first; the OUTCOME still goes.
    local keep = sh.tx
    sh.tx = {}
    sh.life[4] = 3
    sh.E.check(3000000004, 1)
    check(#sh.tx == 1 and sh.tx[1].op == 16, 'no standing sample: no place-first, the outcome is sent')
    sh.tx = keep
    -- Guest: 3 m off is below the continuous threshold, but the outcome waits until this copy stands there.
    sg.positions[1] = {13.6, 1.8, 12.0}
    sg.warp = {{r = 0, h = 1, leaf = 'pinned'}}
    deliver(sh, sg, 34)
    -- The place-first sample carries flag 4 (enemy follow round): it queues the placement at once, ahead of the FIFO.
    check(#sg.P.q == 1 and sg.P.q[1].first, 'a place-first queues the placement at once (3 m: below the continuous threshold)')
    deliver(sh, sg, 16)
    runE(sg, 1)
    check(#sg.P.q + (sg.P.cur and 1 or 0) == 1 and sent(sg, 1, 'ChangeLifeState') == 0,
        'guest: the staff outcome is held while this copy is placed first')
    sg.positions[1] = {10.6, 1.8, 12.0}
    runE(sg, 6)
    check(receiptCount(sg, 'WORLD_WARP_V1') >= 1 and sent(sg, 1, 'ChangeLifeState') == 1 and sg.P.cur == nil and
        logged(sg, 'kind=correct;result=applied') and logged(sg, 'event=place_first;step=done;'),
        'guest: placed where the host has them, then asleep')
    check(#sg.setOut == 0, 'Mother Base corrections never pin SetOutOfArea')
    -- A placement that never lands holds the outcome for at most 4 s.
    local th, tg = staffFixture(true), staffFixture(false)
    th.positions[2] = {5.0, 1.8, 5.0}
    for _ = 1, 3 do th.now = th.now + 600; th.P.tick(th.now) end
    th.tx = {}
    th.life[2] = 3
    th.E.check(3000000002, 1)
    tg.positions[2] = {25.0, 1.8, 5.0}
    tg.warp = {{r = 0, h = 0, leaf = 'pinned'}}
    deliver(th, tg, 34); deliver(th, tg, 16)
    runE(tg, 60)
    check(sent(tg, 2, 'ChangeLifeState') == 0, 'still held at 2 s')
    runE(tg, 70)
    check(sent(tg, 2, 'ChangeLifeState') == 1, 'applied after the 4 s hold even when the placement never lands')
    -- A staff member not realized on this copy is never force-realized: the placement ends, the outcome applies.
    local uh, ug = staffFixture(true), staffFixture(false)
    uh.positions[3] = {6.0, 1.8, 6.0}
    for _ = 1, 3 do uh.now = uh.now + 600; uh.P.tick(uh.now) end
    uh.tx = {}
    uh.life[3] = 3
    uh.E.check(3000000003, 1)
    ug.positions[3] = {26.0, 1.8, 6.0}
    ug.warp = {{r = -1, h = 0, leaf = 'pinned'}}
    deliver(uh, ug, 34); deliver(uh, ug, 16)
    runE(ug, 6)
    check(sent(ug, 3, 'SetForceRealize') == 0 and sent(ug, 3, 'ChangeLifeState') == 1 and logged(ug, 'why=realize_timeout'),
        'Mother Base: an unrealized copy is never force-realized; the outcome applies')
    -- Mother Base staff: corrected tighter than free roam (more than 2 m on two samples, or 5 m once), so both
    -- screens show them on the same spot.
    local function sample(c, seq, h, x, z) c.W.handlers[34]({seq, h, 3, x, 1000, z, 0, 0, 0, 2}, c.now) end
    local dg = staffFixture(false)
    dg.positions[2] = {12.5, 1.8, 0}
    sample(dg, 1, 3000000002, 10000, 0)
    check(#dg.P.q == 0, 'one 2.5 m sample: no correction yet')
    dg.now = dg.now + 3000
    sample(dg, 2, 3000000002, 10000, 0)
    check(#dg.P.q == 1, 'Mother Base staff: 2.5 m on two samples queues a correction')
    local eg = staffFixture(false)
    eg.positions[2] = {15.5, 1.8, 0}
    sample(eg, 1, 3000000002, 10000, 0)
    check(#eg.P.q == 1, 'Mother Base staff: 5.5 m once queues a correction')
    -- A staff member read at exactly (0, 0, 0) (no position) is never shared.
    local zh = staffFixture(true)
    zh.positions[1] = {0, 0, 0}
    for i = 2, 5 do zh.positions[i] = {300.0, 0, 300.0} end
    for _ = 1, 4 do zh.now = zh.now + 600; zh.P.tick(zh.now) end
    check(#placeTx(zh, 34) == 0, 'Mother Base: a staff member at the origin (no position) is not shared')
    -- Without staffPlace (free roam), an outcome applies at once.
    local fh, fg = placeFixture(true), placeFixture(false)
    fg.W.w1 = true
    fh.life[1] = 3
    fh.E.check(ENT1, 1)
    check(#placeTx(fh, 34) == 0, 'free roam: no place-first announcement')
    deliver(fh, fg, 16)
    fg.now = fg.now + 33; fg.E.tick(fg.now)
    check(sent(fg, 1, 'ChangeLifeState') == 1, 'free roam: the outcome applies at once')
    -- Enemy follow (W.followOn, CFG r[19]; docs/world-sync-design.md 7.8) ----------------------------------------------
    local function followFixture(host)
        local c = placeFixture(host)
        c.W.w1, c.W.followOn, c.W.mode, c.W.activeCp = true, true, 2, {[100] = true, [200] = true}
        c.env.vars.playerPosX, c.env.vars.playerPosZ = 0, 0
        c.env.GameObject = {GetGameObjectIdByIndex = function(kind, index) return kind == 'TppPlayer2' and index == 1 and 900 or nil end}
        return c
    end
    local function ticks(c, n, dt) for _ = 1, n do c.now = c.now + (dt or 33); c.P.tick(c.now) end end
    local function sentFor(c, h)
        local out = {}
        for _, p in ipairs(placeTx(c, 34)) do if p.v[2] == h then out[#out + 1] = p end end
        return out
    end
    -- Host scan: 55 m around the host or 60 m around the partner body; a guard 15 m nearer the partner is the guest's.
    local fh = followFixture(true)
    fh.positions[900] = {100.0, 0, 0}          -- the partner body
    fh.positions[1] = {10.0, 1.8, 0}           -- near the host
    fh.positions[2] = {95.0, 1.8, 0}           -- 5 m from the partner, 95 m from the host: the guest's
    fh.positions[3] = {50.0, 1.8, 0}           -- halfway: the host's
    fh.positions[4] = {300.0, 0, 300.0}        -- far from both
    fh.positions[5] = {0, 1.8, 140.0}          -- 140 m from the host, 172 m from the partner
    ticks(fh, 4)
    check(#sentFor(fh, ENT1) == 1 and #sentFor(fh, 3000000003) == 1 and #sentFor(fh, 3000000002) == 0 and
        #sentFor(fh, 3000000004) == 0 and #sentFor(fh, 3000000005) == 0 and sentFor(fh, ENT1)[1].v[3] == 3 and
        sentFor(fh, ENT1)[1].v[8] == 0, 'follow scan: guards near either player, never the guest\'s side, never far ones')
    check(fh.P.patrol.skip[3000000002] and fh.P.patrol.skip[3000000004], 'out of range and the guest\'s side: read again later')
    -- Resampled every 500 ms within 40 m; sent when it moved 1 m or every 2 s.
    fh.tx = {}
    fh.positions[1] = {10.6, 1.8, 0}
    ticks(fh, 16)
    check(#sentFor(fh, ENT1) == 0, 'moved 0.6 m: not sent')
    fh.positions[1] = {11.2, 1.8, 0}
    ticks(fh, 16)
    check(#sentFor(fh, ENT1) == 1 and sentFor(fh, ENT1)[1].v[4] == 11200, 'moved 1.2 m: sent')
    fh.tx = {}
    ticks(fh, 70)
    check(#sentFor(fh, ENT1) == 1, 'standing still: sent again after 2 s')
    -- A refused correction (the guest's native placement off) backs that guard off for 10 s.
    fh.tx = {}
    fh.W.handlers[35]({777, ENT1, 1, 8, 0, 1}, fh.now)
    check(fh.P.patrol.back[ENT1] and logged(fh, 'event=place_ack;seq=777;ent=3000000001;kind=correct;result=refused;why=native_off'),
        'native_off ACK: the guard waits')
    fh.positions[1] = {20.0, 1.8, 0}
    ticks(fh, 150)
    check(#sentFor(fh, ENT1) == 0, 'no sample for that guard within 10 s')
    ticks(fh, 200)
    check(#sentFor(fh, ENT1) >= 1, 'sampled again after 10 s')
    -- Enemy latch: a guard the guest's copy has not realized (why=unrealized) waits 3 s only (it realizes as players near).
    fh.tx = {}
    fh.W.handlers[35]({778, ENT1, 1, 23, 0, 1}, fh.now)
    check(fh.P.patrol.back[ENT1] and fh.P.patrol.back[ENT1] - fh.W.now() <= 3000 and
        logged(fh, 'event=place_ack;seq=778;ent=3000000001;kind=correct;result=refused;why=unrealized'), 'unrealized ACK: 3 s')
    fh.positions[1] = {30.0, 1.8, 0}
    ticks(fh, 60)
    check(#sentFor(fh, ENT1) == 0, 'unrealized: no sample within 2 s')
    ticks(fh, 60)
    check(#sentFor(fh, ENT1) >= 1, 'unrealized: sampled again after 3 s')
    -- Any pairing re-reads every guard (a rejoining guest is placed at once).
    fh.tx = {}
    for _, f in ipairs(fh.W.pairedHooks) do f(true) end
    ticks(fh, 4)
    check(#sentFor(fh, ENT1) == 1 and #sentFor(fh, 3000000003) == 1, 'pairing: the tracked guards are announced again')
    -- The scan line every 10 s.
    ticks(fh, 320)
    check(logged(fh, 'event=place_scan;ups='), 'the scan cost line')
    -- Without follow, play shares no free roam guard (its guest refuses them); the lab still does (the lab key).
    local nf = followFixture(true)
    nf.W.followOn = false
    nf.positions[1] = {10.0, 1.8, 0}
    ticks(nf, 4)
    check(#placeTx(nf, 34) == 0, 'play without follow: no free roam corrections')
    nf.W.mode = 1
    ticks(nf, 20)
    check(#placeTx(nf, 34) >= 1, 'lab without follow: the 0.14.7 scan')
    -- Place first: the host's own outcome of a standing guard announces where he is (kind 3, flag 4) before the OUTCOME.
    local ph2 = followFixture(true)
    ph2.positions[1] = {10.0, 1.8, 0}
    ticks(ph2, 4)
    ph2.tx = {}
    ph2.life[1] = 3
    ph2.E.check(ENT1, 1)
    check(#ph2.tx == 2 and ph2.tx[1].op == 34 and ph2.tx[1].v[3] == 3 and ph2.tx[1].v[8] == 4 and ph2.tx[1].v[4] == 10000 and
        ph2.tx[2].op == 16 and logged(ph2, 'event=place_first;step=tx;'), 'follow: place first (flag 4), then the OUTCOME')
    -- A guard going up under a balloon (rising over 3 m faster than 2 m/s) is not sampled; his place-first keeps the
    -- standing sample (run 3e01f334 sent 446 m).
    local ph3 = followFixture(true)
    ph3.positions[2] = {12.0, 1.8, 0}
    ticks(ph3, 4)
    ph3.tx = {}
    ph3.positions[2] = {12.0, 30.0, 0}
    ticks(ph3, 40)
    check(#sentFor(ph3, 3000000002) == 0, 'a guard rising fast is not sampled')
    ph3.life[2] = 3
    ph3.E.check(3000000002, 5)
    check(ph3.tx[1] and ph3.tx[1].op == 34 and ph3.tx[1].v[5] == 1400 and ph3.tx[1].v[4] == 12000,
        'his place-first: the standing x, z and ground y (+ the lift)')
    local ph4 = followFixture(true)
    ph4.positions[2] = {12.0, 1.8, 0}
    ticks(ph4, 4)
    ph4.tx = {}
    ph4.positions[2] = {12.0, 5.0, 0}
    ticks(ph4, 130)
    check(#sentFor(ph4, 3000000002) >= 1, 'a guard 3.2 m higher after 2 s (stairs, a ladder) is sampled')
    -- Guest thresholds. This player at the origin looking +z (camera yaw 0).
    local function guestFix(yaw)
        local c = followFixture(false)
        c.env.vars.playerCameraRotation = {[0] = 0, [1] = yaw or 0}
        c.positions[900] = {0, 0, 5.0}
        return c
    end
    local function sample(c, seq, h, x, z, flags) c.W.handlers[34]({seq, h, 3, x, 1000, z, 0, flags or 0, 0, 2}, c.now) end
    local fgA = guestFix(0)
    fgA.positions[1] = {0, 1.8, 20.0}             -- on screen, 20 m ahead
    sample(fgA, 1, ENT1, 1000, 20000)              -- 1 m: seen, nothing
    fgA.now = fgA.now + 2000
    sample(fgA, 2, ENT1, 4000, 20000)              -- 4 m on screen: below 5 m
    fgA.now = fgA.now + 2000
    sample(fgA, 3, ENT1, 4000, 20000)
    check(#fgA.P.q == 0, 'on screen: 4 m twice is left alone')
    fgA.now = fgA.now + 2000
    sample(fgA, 4, ENT1, 6000, 20000)
    fgA.now = fgA.now + 2000
    sample(fgA, 5, ENT1, 6000, 20000)
    check(#fgA.P.q == 1 and logged(fgA, ';view=on'), 'on screen: 6 m twice is corrected')
    local fgB = guestFix(180)                      -- looking away: the guard is behind
    fgB.positions[1] = {0, 1.8, 20.0}
    sample(fgB, 1, ENT1, 1000, 20000)
    fgB.now = fgB.now + 2000
    sample(fgB, 2, ENT1, 2400, 20000)
    check(#fgB.P.q == 0, 'off screen: 2.4 m (extrapolated 2.5 m) is left alone')
    fgB.now = fgB.now + 2000
    sample(fgB, 3, ENT1, 3000, 20000)
    check(#fgB.P.q == 1 and logged(fgB, ';view=off'), 'off screen: one sample over 2.5 m is corrected')
    -- Enemy round 8 track A (quiet_place, docs/world-sync-design.md 7.22): an off-screen correction names the quiet move
    -- (" 1"; the native decides); an on-screen one does not (run 0d801772: guard 246's 56 m set-transform correction off
    -- screen went to action kind 3 within 0.2 s, then the radio report).
    fgB.warp = {{r = 0, h = 1, leaf = 'quiet'}}
    ticks(fgB, 4)
    check(receipt(fgB, "'MGS_COOP_WORLD_WARP_V1' -- 3 1 1") and logged(fgB, 'event=place;seq=3;ent=' .. ENT1 .. ';kind=correct;result=applied'),
        'quiet_place: an off-screen correction asks for the quiet move and lands')
    -- Its position is read back 0.5 s and 2 s after (src=quiet), x/z (dxz_m) and y (dy_m) against the target.
    ticks(fgB, 20)
    check(logged(fgB, 'event=readback;ent=' .. ENT1 .. ';src=quiet;at=500;') and logged(fgB, ';dxz_m=3.00'),
        'quiet_place: the quiet move is read back (x/z and y)')
    -- Enemy round 9 track A (design 7.24): a third read-back 5 s after a quiet move (the walk-on row; run 1befcef4: guard
    -- 1904171355 walked 9.4-14.1 m on after his quiet moves).
    ticks(fgB, 160)
    check(logged(fgB, 'event=readback;ent=' .. ENT1 .. ';src=quiet;at=2000;') and logged(fgB, 'event=readback;ent=' .. ENT1 .. ';src=quiet;at=5000;'),
        'quiet_place: the quiet move is read back at 2 s and 5 s too')
    -- Enemy round 11 track A (design 7.31): a fourth read-back 10 s after a quiet move (a moved walker keeps walking: his
    -- gate is measured against the owner copy), and each quiet read-back gives the x/z distance to the owner copy (the
    -- host's last sample moved on by its speed for at most 400 ms: dho_m; its age: oage_ms).
    ticks(fgB, 200)
    check(logged(fgB, 'event=readback;ent=' .. ENT1 .. ';src=quiet;at=10000;') and logged(fgB, ';dho_m=') and logged(fgB, ';oage_ms='),
        'quiet_place: the quiet move is read back at 10 s too, and against the owner copy')
    fgA.warp = {{r = 0, h = 1, leaf = 'engine'}}
    ticks(fgA, 4)
    check(receipt(fgA, "'MGS_COOP_WORLD_WARP_V1' -- 5 1") and not receipt(fgA, "'MGS_COOP_WORLD_WARP_V1' -- 5 1 1"),
        'quiet_place: an on-screen correction keeps the set-transform move')
    local fgC = guestFix(180)
    fgC.positions[1] = {0, 1.8, 20.0}
    sample(fgC, 1, ENT1, 1000, 20000)
    fgC.now = fgC.now + 2000
    sample(fgC, 2, ENT1, 5500, 20000)
    check(#fgC.P.q == 1, 'off screen: 5.5 m once is corrected')
    -- 1.5 s apart per guard.
    fgC.P.q = {}
    fgC.now = fgC.now + 600
    sample(fgC, 3, ENT1, 9000, 20000)
    check(#fgC.P.q == 0, 'off screen: within 1 s of the last correction, none')
    fgC.now = fgC.now + 500
    sample(fgC, 4, ENT1, 9000, 20000)
    check(#fgC.P.q == 1, 'off screen: after 1 s, corrected')
    -- On screen: 1.5 s apart.
    local fgS = guestFix(0)
    fgS.positions[1] = {0, 1.8, 20.0}
    sample(fgS, 1, ENT1, 1000, 20000)
    fgS.now = fgS.now + 2000
    sample(fgS, 2, ENT1, 9000, 20000)
    check(#fgS.P.q == 1, 'on screen: 9 m once is corrected')
    fgS.P.q = {}
    fgS.now = fgS.now + 1100
    sample(fgS, 3, ENT1, 17000, 20000)
    check(#fgS.P.q == 0, 'on screen: within 1.5 s of the last correction, none')
    fgS.now = fgS.now + 500
    sample(fgS, 4, ENT1, 17000, 20000)
    check(#fgS.P.q == 1, 'on screen: after 1.5 s, corrected')
    -- Never within 3 m of this player; never a copy 250 m from both players.
    local fgD = guestFix(180)
    fgD.positions[1] = {0, 1.8, 2.0}
    sample(fgD, 1, ENT1, 1000, 2000)
    fgD.now = fgD.now + 2000
    sample(fgD, 2, ENT1, 9000, 2000)
    check(#fgD.P.q == 0 and logged(fgD, ';why=near_player'), 'never within 3 m of this player')
    -- A copy more than 10 m under the host's is corrected at once, on screen, right by x, z (run 51f8e90f: y -80 m).
    local fgM = guestFix(0)
    fgM.positions[1] = {0, -60.0, 20.0}
    sample(fgM, 1, ENT1, 0, 20000)
    check(#fgM.P.q == 1 and logged(fgM, ';view=on;why=fall;dy_m=-61.0'), 'a copy 61 m under the host copy is corrected at once')
    local fgN = guestFix(0)
    fgN.positions[1] = {0, -6.0, 20.0}
    sample(fgN, 1, ENT1, 0, 20000)
    check(#fgN.P.q == 0 and not logged(fgN, 'why=fall'), '7 m under: left to the thresholds')
    local fgE = guestFix(180)
    fgE.positions[1] = {300.0, 1.8, 0}
    sample(fgE, 1, ENT1, 300000, 1000)
    fgE.now = fgE.now + 2000
    sample(fgE, 2, ENT1, 40000, 0)
    check(#fgE.P.q == 0 and logged(fgE, ';why=far'), 'never a copy 250 m from both players')
    -- The first sample after pairing corrects beyond 2.5 m at once; an unpairing forgets which guards were seen.
    local fgF = guestFix(0)
    fgF.positions[1] = {0, 1.8, 20.0}
    sample(fgF, 1, ENT1, 3000, 20000)
    check(#fgF.P.q == 1 and logged(fgF, ';why=pair'), 'first sample after pairing: 3 m on screen is corrected at once')
    fgF.P.q = {}
    for _, f in ipairs(fgF.W.unpairedHooks) do f(1) end
    check(next(fgF.P.div) == nil, 'unpairing forgets the guards seen')
    -- Place first (flag 4): from 1 m, at once, ahead of the FIFO; under 1 m nothing.
    local fgG = guestFix(0)
    fgG.positions[1] = {0, 1.8, 20.0}
    fgG.positions[2] = {0, 1.8, -20.0}
    sample(fgG, 1, ENT1, 0, 20000)
    sample(fgG, 2, 3000000002, 0, -20000)
    fgG.now = fgG.now + 2000
    sample(fgG, 3, 3000000002, 6000, -20000)       -- queued (5 m once off screen... behind: 6 m)
    sample(fgG, 4, ENT1, 1200, 20000, 4)            -- place first
    check(#fgG.P.q == 2 and fgG.P.q[1].h == ENT1 and fgG.P.q[1].first, 'a place-first goes ahead of the FIFO')
    local fgH = guestFix(0)
    fgH.positions[1] = {0, 1.8, 20.0}
    sample(fgH, 1, ENT1, 500, 20000, 4)
    check(#fgH.P.q == 0 and logged(fgH, ';why=first'), 'a place-first under 1 m moves nothing')
    -- The host copy's speed: the second sample is moved on by min(age + 100 ms, 400 ms).
    local fgI = guestFix(180)
    fgI.positions[1] = {2.2, 1.8, 10.0}
    sample(fgI, 1, ENT1, 0, 10000)
    fgI.now = fgI.now + 1000
    fgI.logs = {}
    sample(fgI, 2, ENT1, 2000, 10000)              -- 2 m/s: extrapolated to 2.2 m
    fgI.now = fgI.now + 2000
    sample(fgI, 3, ENT1, 2000, 10000)
    check(#fgI.P.q == 0, 'extrapolated by the host copy\'s speed: this copy is where the host\'s will be')
    -- No combat nudge; the place error line once a second.
    local fgJ = guestFix(0)
    fgJ.positions[1] = {0, 1.8, 20.0}
    sample(fgJ, 1, ENT1, 1500, 20000, 1)
    fgJ.now = fgJ.now + 1100
    fgJ.P.tick(fgJ.now)
    check(sent(fgJ, 1, 'SetDiscoveryPlayer') == 0 and logged(fgJ, 'event=place_combat;') and
        logged(fgJ, 'event=place_err;n=1;h=0,1,0,0,0,0,0;p50=1.5;p95=1.5;max=1.5;fix=0;q=0'), 'no nudge; the place error line')
    -- A combat-flagged correction never marks the guard as come (run f2d2db9b: his later samples were all ignored).
    local fgL = guestFix(180)
    fgL.positions[1] = {0, 1.8, 20.0}
    fgL.warp = {{r = 0, h = 1, leaf = 'engine'}}
    sample(fgL, 1, ENT1, 1000, 20000, 1)
    fgL.now = fgL.now + 2000
    sample(fgL, 2, ENT1, 9000, 20000, 1)
    check(#fgL.P.q == 1, 'combat sample: corrected')
    for _ = 1, 5 do fgL.now = fgL.now + 33; fgL.P.tick(fgL.now) end
    check(fgL.P.cur == nil and not fgL.P.come[ENT1] and logged(fgL, 'kind=correct;result=applied'),
        'the combat correction landed and the guard is not come')
    fgL.logs = {}
    fgL.now = fgL.now + 2000
    sample(fgL, 3, ENT1, 18000, 20000, 1)
    check(logged(fgL, 'event=place_div;seq=3;'), 'his next sample is still compared')
    -- The partner position from the native PEER receipt (the remote pose), else the partner body.
    local pr = followFixture(true)
    local recv = pr.W.receipt
    pr.W.receipt = function(source)
        if source == "return 'MGS_COOP_WORLD_PEER_V1'" then return 1, 70000, 5000, 80 end
        return recv(source)
    end
    local qx, qz = pr.P.partnerXZ(pr.now)
    check(qx == 70000 and qz == 5000 and logged(pr, 'event=place_partner;known=1;src=peer'), 'the partner from PEER')
    pr.now = pr.now + 600
    pr.W.receipt = recv
    pr.positions[900] = {-3.0, 0, 4.0}
    qx, qz = pr.P.partnerXZ(pr.now)
    check(qx == -3000 and qz == 4000, 'an older loader: the partner body')
    -- A host outcome waits for this copy's placement at most 1.5 s in free roam.
    local fgK = guestFix(180)
    fgK.positions[1] = {0, 1.8, 20.0}
    fgK.warp = {{r = 0, h = 0, leaf = 'engine'}}
    sample(fgK, 1, ENT1, 1000, 20000)
    fgK.now = fgK.now + 100
    sample(fgK, 2, ENT1, 6000, 20000, 4)
    fgK.W.handlers[16]({ENT1, 1, 2, 1, 3, 0, 0, 0, 6000, 1000, 20000}, fgK.now)
    for _ = 1, 30 do fgK.now = fgK.now + 33; fgK.E.tick(fgK.now); fgK.P.tick(fgK.now) end
    check(sent(fgK, 1, 'ChangeLifeState') == 0, 'held while the placement runs (1 s)')
    for _ = 1, 30 do fgK.now = fgK.now + 33; fgK.E.tick(fgK.now); fgK.P.tick(fgK.now) end
    check(sent(fgK, 1, 'ChangeLifeState') == 1, 'applied after the 1.5 s hold even when the placement never lands')
    -- Enemy sync M3 (W.ownerOn, docs/world-sync-design.md 7.9) ---------------------------------------------------------
    -- Host: the guest owns a guard 15 m nearer its player (OWNER 73, why near); a decision holds 5 s.
    local oh = followFixture(true)
    oh.W.ownerOn = true
    oh.positions[900] = {100.0, 0, 0}          -- the partner body
    oh.positions[2] = {95.0, 1.8, 0}           -- 5 m from the partner, 95 m from the host
    oh.positions[3] = {50.0, 1.8, 0}           -- halfway: the host's
    ticks(oh, 4)
    local own = placeTx(oh, 73)
    check(#own == 1 and own[1].v[1] == 3000000002 and own[1].v[2] == 1 and own[1].v[3] == 1 and
        logged(oh, 'event=owner;ent=3000000002;owner=guest;why=near') and #sentFor(oh, 3000000002) == 0 and
        #sentFor(oh, 3000000003) == 1, 'M3: the guard near the guest belongs to the guest (OWNER), never corrected by the host')
    oh.tx = {}
    oh.positions[900] = {-100.0, 0, 0}         -- the guest walks away and the guard comes to 40 m of the host: the host's
    oh.positions[2] = {40.0, 1.8, 0}           -- again after the 5 s hold
    ticks(oh, 150)
    check(#placeTx(oh, 73) == 0, 'M3: an owner decision holds 10 s')
    ticks(oh, 250)
    own = placeTx(oh, 73)
    check(#own == 1 and own[1].v[2] == 0 and logged(oh, 'event=owner;ent=3000000002;owner=host'), 'M3: back to the host after 10 s')
    -- ENGAGE (74) is logged, never an owner decision (run 4ad309a3: Damage named the local player for guards nobody shot);
    -- a guard within 20 m of the host's player stays the host's even 15 m nearer the guest; between 5 and 15 m nearer the
    -- guest a guard keeps his owner (the hysteresis band).
    local oe = followFixture(true)
    oe.W.ownerOn = true
    oe.positions[900] = {100.0, 0, 0}
    oe.positions[3] = {50.0, 1.8, 0}
    oe.W.handlers[74]({3000000003, 1}, oe.now)
    ticks(oe, 40)
    check(logged(oe, 'event=engage_rx;ent=3000000003') and #placeTx(oe, 73) == 0, 'M3: ENGAGE is logged only')
    local ob = followFixture(true)
    ob.W.ownerOn = true
    ob.positions[900] = {40.0, 0, 0}
    ob.positions[3] = {18.0, 1.8, 0}           -- 18 m from the host, 22 m from the guest... and 22 > 18: the host's
    ob.positions[2] = {19.0, 1.8, 0}
    ob.positions[900] = {50.0, 0, 0}           -- 19 m from the host, 31 m from the guest
    ticks(ob, 40)
    check(#placeTx(ob, 73) == 0, 'M3: within 20 m of the host: the host')
    local oy = followFixture(true)
    oy.W.ownerOn = true
    oy.positions[900] = {100.0, 0, 0}
    oy.positions[2] = {90.0, 1.8, 0}           -- 80 m nearer the guest: the guest's
    ticks(oy, 4)
    check(#placeTx(oy, 73) == 1, 'M3: the guest owns him')
    oy.tx = {}
    oy.positions[2] = {54.0, 1.8, 0}           -- 54 m from the host, 46 m from the guest (8 m nearer): kept
    ticks(oy, 400)
    check(#placeTx(oy, 73) == 0, 'M3: 8 m nearer the guest: the owner is kept (the hysteresis band)')
    oy.positions[2] = {51.0, 1.8, 0}           -- 2 m nearer the guest: back to the host
    ticks(oy, 60)
    check(#placeTx(oy, 73) == 1 and placeTx(oy, 73)[1].v[2] == 0, 'M3: under 5 m nearer the guest: the host')
    -- Host: the guest's ENEMY_PLACE (71) moves the host's copy (off its screen: one sample over 2.5 m), WARP_HOST_V1, ACK 72.
    local hp = followFixture(true)
    hp.W.ownerOn = true
    hp.env.vars.playerCameraRotation = {[0] = 0, [1] = 0}
    hp.positions[900] = {100.0, 0, 0}
    hp.positions[2] = {95.0, 1.8, 0}
    hp.warp = {{r = 0, h = 1, leaf = 'engine'}}
    hp.W.handlers[71]({5, 3000000002, 3, 100000, 1400, 0, 0, 0, 0, 2}, hp.now)
    check(#hp.P.q == 1 and hp.P.q[1].host and logged(hp, 'event=place_div;seq=5;ent=3000000002;div_m=5.0'),
        'M3: the host queues its own copy correction')
    hp.positions[2] = {100.0, 1.8, 0}
    ticks(hp, 6)
    local ack = placeTx(hp, 72)
    check(hp.receipts[#hp.receipts]:find("'MGS_COOP_WORLD_WARP_HOST_V1' -- 5 2", 1, true) and
        logged(hp, 'event=hplace;seq=5;ent=3000000002;kind=correct;result=applied') and #ack == 1 and ack[1].v[3] == 0 and
        #placeTx(hp, 35) == 0, 'M3: the host receipt, event=hplace, ENEMY_PLACE_ACK (never ENEMY_DEBUG_ACK)')
    -- Enemy gaps (place_sink): the host reads its placed copy 2 s after landing although its FIFO is empty (runs d4460ea5,
    -- 2d967b8a checked at 3.3 s and 8 s); a copy 3.3 m under the owner's newest sample is a sink, with owner_dy_m and moved_m.
    check(#hp.P.q == 0 and hp.P.cur == nil and hp.P.sink[3000000002], 'place_sink: the host copy landed, FIFO empty')
    hp.positions[2] = {100.5, -1.5, 0}
    ticks(hp, 70)
    check(logged(hp, 'event=place_sink;seq=5;ent=3000000002;land_dy_m=0.40;dy_m=-2.90;owner_dy_m=-3.30;moved_m=0.5'),
        'place_sink: the host checks every tick, against the owner (owner_dy_m) and the travel (moved_m)')
    -- A copy 3 m under its landing but level with its owner (the owner went down a step too) is a drop, not a sink.
    local hq = followFixture(true)
    hq.W.ownerOn = true
    hq.positions[900] = {100.0, 0, 0}
    hq.positions[2] = {95.0, 1.8, 0}
    hq.warp = {{r = 0, h = 1, leaf = 'engine'}}
    hq.W.handlers[71]({5, 3000000002, 3, 100000, 1400, 0, 0, 0, 0, 2}, hq.now)
    hq.positions[2] = {100.0, 1.8, 0}
    ticks(hq, 6)
    hq.W.handlers[71]({6, 3000000002, 3, 100000, -1600, 0, 0, 0, 0, 2}, hq.now)
    hq.positions[2] = {100.0, -1.2, 0}
    ticks(hq, 70)
    check(logged(hq, 'event=place_drop;seq=5;ent=3000000002;land_dy_m=0.40;dy_m=-2.60;owner_dy_m=0.00;moved_m=0.0') and
        not logged(hq, 'event=place_sink;'), 'place_drop: down with its owner')
    -- place_lift (CFG r[19] bit 128, W.liftOn): placements and the stream carry the owner's own GetPosition y (LIFT 800).
    local lf = followFixture(true)
    lf.W.liftOn = true
    lf.P.reset()
    lf.W.handlers[OP.PAIRED]({7, 30010, lf.W.roster, 5, 1, 3})
    lf.positions[1] = {10.0, 1.8, 0}
    ticks(lf, 4)
    check(lf.P.LIFT == 800 and #sentFor(lf, ENT1) == 1 and sentFor(lf, ENT1)[1].v[5] == 1800,
        'place_lift: the scan sends GetPosition y (1800), not y - 800 + 400 (1400)')
    local nl = followFixture(true)
    nl.P.reset()
    check(nl.P.LIFT == 400, 'without place_lift: LIFT 400')
    -- A guest place-first (flag 4) moves the host's copy only while the host's player is over 40 m away or the gap is
    -- under 3 m; meanwhile the host's apply of that guest outcome waits (W.placeHoldHost, at most 1.5 s).
    local hv = followFixture(true)
    hv.W.ownerOn = true
    hv.positions[900] = {100.0, 0, 0}
    hv.positions[2] = {30.0, 1.8, 0}            -- 30 m from the host
    hv.W.handlers[71]({6, 3000000002, 3, 36000, 1400, 0, 0, 4, 0, 2}, hv.now)
    check(#hv.P.q == 0 and logged(hv, 'why=visible'), 'M3: the host never sees a guard teleport (30 m, 6 m off)')
    hv.W.handlers[71]({7, 3000000002, 3, 32000, 1400, 0, 0, 4, 0, 2}, hv.now)
    check(#hv.P.q == 1 and hv.P.q[1].first and hv.W.placeHoldHost(3000000002, hv.now) and
        not hv.W.placeHoldHost(3000000002, hv.now + 1600), 'M3: 2 m off: placed first; the host apply waits at most 1.5 s')
    -- Guest: OWNER gives it the guard; its copy's samples go out (ENEMY_PLACE: y - 800 + the lift), 1 m or 2 s; a refused
    -- place backs off 10 s; ENGAGE on its own hit; its place-first (flag 4) before its own outcome.
    local og = guestFix(0)
    og.W.ownerOn = true
    og.positions[1] = {0, 1.8, 20.0}
    og.W.handlers[73]({ENT1, 1, 1}, og.now)
    check(og.P.owned[ENT1] and logged(og, 'event=owner_rx;ent=3000000001;owner=guest'), 'M3: the guest owns the guard')
    ticks(og, 4)
    local pl = placeTx(og, 71)
    check(#pl == 1 and pl[1].v[2] == ENT1 and pl[1].v[3] == 3 and pl[1].v[4] == 0 and pl[1].v[5] == 1400 and pl[1].v[6] == 20000,
        'M3: ENEMY_PLACE with the feet y plus the lift')
    og.tx = {}
    og.positions[1] = {0, 1.8, 21.2}
    ticks(og, 16)
    check(#placeTx(og, 71) == 1, 'M3: moved 1.2 m: sent')
    og.tx = {}
    ticks(og, 70)
    check(#placeTx(og, 71) == 1, 'M3: standing still: sent again after 2 s')
    og.tx = {}
    og.W.handlers[72]({9, ENT1, 1, 20, 0, 0}, og.now)
    ticks(og, 150)
    check(#placeTx(og, 71) == 0 and logged(og, 'event=place_ack;side=host;seq=9;ent=3000000001;result=refused;why=far'),
        'M3: a refused place (far) backs the guard off')
    -- Enemy latch: the host's copy not realized there (why=unrealized, run e583ee62's M3 relays) waits 3 s, not 10.
    local ou = guestFix(0)
    ou.W.ownerOn = true
    ou.positions[1] = {0, 1.8, 20.0}
    ou.W.handlers[73]({ENT1, 1, 1}, ou.now)
    ticks(ou, 4)
    ou.tx = {}
    ou.W.handlers[72]({9, ENT1, 1, 23, 0, 0}, ou.now)
    ou.positions[1] = {0, 1.8, 25.0}
    ticks(ou, 60)
    check(#placeTx(ou, 71) == 0 and logged(ou, 'event=place_ack;side=host;seq=9;ent=3000000001;result=refused;why=unrealized'),
        'M3: unrealized on the host: no place within 2 s')
    ticks(ou, 60)
    check(#placeTx(ou, 71) >= 1, 'M3: placed again after 3 s')
    -- The host side: its copy answered relayed (out=0 handled=0) -> refused why=unrealized in the ACK, never native_other.
    local hr = followFixture(true)
    hr.W.ownerOn = true
    hr.env.vars.playerCameraRotation = {[0] = 0, [1] = 0}
    hr.positions[900] = {100.0, 0, 0}
    hr.positions[2] = {95.0, 1.8, 0}
    hr.warp = {{refused = 'relayed'}}
    hr.W.handlers[71]({5, 3000000002, 3, 100000, 1400, 0, 0, 0, 0, 2}, hr.now)
    ticks(hr, 6)
    local ra = placeTx(hr, 72)
    check(#ra == 1 and ra[1].v[3] == 1 and hr.P.WHY[ra[1].v[4]] == 'unrealized' and
        logged(hr, 'event=hplace;seq=5;ent=3000000002;kind=correct;result=refused;why=unrealized;native=relayed'),
        'M3: a relayed host copy is refused why=unrealized')
    for _, f in ipairs(og.W.observers) do f('Damage', ENT1, 'local') end
    check(#placeTx(og, 74) == 0, 'M3: no ENGAGE (Damage named the local player for guards nobody shot)')
    og.tx = {}
    og.W.guestPlaceAnnounce(ENT1)
    pl = placeTx(og, 71)
    check(#pl == 1 and pl[1].v[8] == 4 and pl[1].v[5] == 1400 and logged(og, 'event=place_first;step=tx;side=guest'),
        'M3: the guest place-first')
    og.W.handlers[73]({ENT1, 0, 3}, og.now)
    check(not og.P.owned[ENT1] and logged(og, 'event=owner_rx;ent=3000000001;owner=host'), 'M3: back to the host')
    og.tx = {}
    og.W.guestPlaceAnnounce(ENT1)
    check(#placeTx(og, 71) == 0, 'M3: no place-first for a guard the host owns')
    -- M3 calm-down: the host's phase decrease goes out (PHASE flag 2); the guest lowers its CP unless its own fight
    -- raised it in the last 5 s.
    local function setPhases(c, phase)
        local n = 0
        for _, cmd in ipairs(c.commands) do if cmd[2] == 'SetPhase' and cmd[3] == phase then n = n + 1 end end
        return n
    end
    local pdh = followFixture(true)
    pdh.W.ownerOn = true
    pdh.W.phase.onLocal(100, 13)
    pdh.tx = {}
    pdh.W.phase.onLocal(100, 11)
    local ph = placeTx(pdh, 18)
    check(#ph == 1 and ph[1].v[1] == 100 and ph[1].v[2] == 1 and ph[1].v[6] == 2 and logged(pdh, 'src=down'),
        'M3: the host calm-down goes out')
    local pdo = followFixture(true)
    pdo.W.phase.onLocal(100, 13)
    pdo.tx = {}
    pdo.W.phase.onLocal(100, 11)
    check(#placeTx(pdo, 18) == 0, 'without M3 a host decrease stays local')
    local pdg = followFixture(false)
    pdg.W.ownerOn = true
    pdg.W.cp[100].localPhase = 3
    pdg.W.handlers[18]({100, 1, 5, 0, 0, 2}, pdg.now)
    check(setPhases(pdg, 11) == 1 and pdg.W.cp[100].hostPhase == 1 and logged(pdg, 'src=down;applied=1'),
        'M3: the guest comes down with the host')
    local pdf = followFixture(false)
    pdf.W.ownerOn = true
    pdf.W.cp[100].localPhase = 3
    pdf.W.cp[100].ownAt = pdf.now - 1000
    pdf.W.handlers[18]({100, 1, 5, 0, 0, 2}, pdf.now)
    check(setPhases(pdf, 11) == 0 and logged(pdf, 'src=down;applied=0'), 'M3: its own fight in the last 5 s keeps it up')
    -- Enemy sync M4 (W.snapOn, docs/world-sync-design.md 7.10) ---------------------------------------------------------
    -- The owner streams SOLDIER_SNAP (75) of its NORMAL guards within 60 m of the other player: 10 Hz within 30 m.
    local sh = followFixture(true)
    sh.W.snapOn = true
    sh.positions[900] = {10.0, 0, 0}
    sh.positions[1] = {5.0, 1.8, 0}
    ticks(sh, 31)
    local snaps = {}
    for _, p in ipairs(placeTx(sh, 75)) do if p.v[2] == 1 then snaps[#snaps + 1] = p end end
    print('M4 snaps', #snaps, snaps[1] and snaps[1].v[3], snaps[1] and snaps[1].v[4], snaps[1] and snaps[1].v[5], #sh.tx)
    check(#snaps >= 7 and #snaps <= 11 and snaps[1].v[3] == 5000 and snaps[1].v[4] == 1400 and snaps[1].v[5] == 0,
        'M4: about 8-10 Hz within 30 m (100 ms on 33 ms updates), the feet y plus the lift')
    sh.tx = {}
    sh.W.ownerOn = true
    sh.P.own[ENT1] = {o = 1, at = sh.now}
    ticks(sh, 31)
    local none = 0
    for _, p in ipairs(placeTx(sh, 75)) do if p.v[2] == 1 then none = none + 1 end end
    check(none == 0, 'M4: never a guard the guest owns')
    -- The receiver: one DRIVE_V1 per guard and update with its own position; the stream's guard is left alone by the
    -- corrections; a stale stream backs off 500 ms.
    local sg = guestFix(180)
    sg.W.snapOn = true
    sg.positions[1] = {0, 1.8, 20.0}
    ticks(sg, 2)
    check(sg.drives and sg.drives[1] == "return 'MGS_COOP_WORLD_DRIVE_V1' -- 1 1 0 1800 20000" and sg.W.snapDriving(ENT1, sg.now),
        'M4: the receiver drives with its own position')
    sample(sg, 1, ENT1, 5000, 20000)
    check(#sg.P.q == 0 and logged(sg, 'why=driven'), 'M4: a driven guard is never corrected')
    local ss = guestFix(180)
    ss.W.snapOn = true
    ss.positions[1] = {0, 1.8, 20.0}
    ss.driveReply = {refused = 'stale'}
    ticks(ss, 2)
    local n1 = #ss.drives
    ticks(ss, 10)
    check(#ss.drives == n1 and not ss.W.snapDriving(ENT1, ss.now), 'M4: a stale stream backs the guard off')
    ticks(ss, 10)
    check(#ss.drives > n1, 'M4: and tries again after 500 ms')
    -- Enemy harden (design 7.30): a guard seated in a vehicle on this copy (GetVehicleGameObjectId, the placements'
    -- vehicle_local) is never driven: no xz step and no vert snap would pull him out of the seat (f256bf29: 25605's two
    -- riders drove past the guest 12 m off, its riders streamed and driven); on foot again he is driven.
    ;(function()
        local sv = guestFix(180)
        sv.W.snapOn = true
        sv.positions[1] = {0, 1.8, 20.0}
        sv.vehicles = {[1] = 77}
        ticks(sv, 10)
        local function drivesOf(c, slot)
            local n = 0
            for _, s in ipairs(c.drives or {}) do if s:find(" -- " .. slot .. " ", 1, true) then n = n + 1 end end
            return n
        end
        check(drivesOf(sv, 1) == 0 and not sv.W.snapDriving(ENT1, sv.now) and logCount(sv, 'event=snap_seated;ent=3000000001;id=1;on=1') == 1,
            'M4: a guard seated in a vehicle here is never driven (one snap_seated line)')
        sv.vehicles = {}
        ticks(sv, 20)
        check(drivesOf(sv, 1) > 0 and logCount(sv, 'event=snap_seated;ent=3000000001;id=1;on=0') == 1, 'M4: out of the vehicle he is driven again')
        sv.vehicles = {[1] = 77}
        local n2 = drivesOf(sv, 1)
        ticks(sv, 20)
        check(drivesOf(sv, 1) <= n2 + 8 and logCount(sv, 'event=snap_seated;ent=3000000001;id=1;on=1') == 2, 'M4: seated again: left alone within 250 ms')
        ticks(sv, 300)
        check(logged(sv, 'event=snap_stats;') and logged(sv, ';seated='), 'M4: snap_stats counts the seated skips')
    end)()
    -- Enemy gaps: a driven copy 1.5 m under its owner for 2 s logs event=drive_sink once (v: its y minus the target's, cm).
    local sk = guestFix(180)
    sk.W.snapOn = true
    sk.positions[1] = {0, 1.8, 20.0}
    sk.driveReply = {e = 50, s = 1, r = 0, h = 1, v = -300}
    ticks(sk, 30)
    check(not logged(sk, 'event=drive_sink;'), 'drive_sink: not before 2 s')
    ticks(sk, 40)
    check(logCount(sk, 'event=drive_sink;ent=3000000001;off_m=-3.40;e_cm=50') == 1, 'drive_sink: once, 3.4 m under the owner')
    local sl = guestFix(180)
    sl.W.snapOn = true
    sl.positions[1] = {0, 1.8, 20.0}
    sl.driveReply = {e = 50, s = 1, r = 0, h = 1, v = 40}
    ticks(sl, 80)
    check(not logged(sl, 'event=drive_sink;'), 'drive_sink: level with the owner (v = +0.4 m with LIFT 400) is none')
    -- Enemy sync M6 (W.bodyOn, docs/world-sync-design.md 7.12) ---------------------------------------------------------
    -- A body carried more than 1.5 m and at rest 1.5 s is reported (host: ENEMY_DEBUG kind 3 flag 8, 0.2 m over it).
    local bh = followFixture(true)
    bh.W.bodyOn = true
    bh.life[1] = 3
    bh.positions[1] = {10.0, 0.3, 0}
    ticks(bh, 60)
    local function bodyTx(c, op)
        local out = {}
        for _, p in ipairs(placeTx(c, op)) do if p.v[8] == 8 then out[#out + 1] = p end end
        return out
    end
    check(#bodyTx(bh, 34) == 0, 'M6: a body where it fell is not reported')
    bh.positions[1] = {15.0, 0.3, 0}
    ticks(bh, 70)
    local bt = bodyTx(bh, 34)
    check(#bt == 1 and bt[1].v[2] == ENT1 and bt[1].v[4] == 15000 and bt[1].v[5] == 500 and logged(bh, 'event=body_tx;ent=3000000001'),
        'M6: a body dropped 5 m away and at rest is reported once')
    ticks(bh, 70)
    check(#bodyTx(bh, 34) == 1, 'M6: once')
    -- The receiver places its own body there (flag 8: through the FIFO, a dead one too).
    local bg = guestFix(180)
    bg.W.bodyOn = true
    bg.life[1] = 1
    bg.positions[1] = {0, 0.3, 20.0}
    bg.warp = {{r = 0, h = 1, leaf = 'engine'}}
    bg.W.handlers[34]({41, ENT1, 3, 5000, 500, 20000, 0, 8, 0, 2}, bg.now)
    check(#bg.P.q == 1 and bg.P.q[1].body and logged(bg, 'event=place_body;seq=41;ent=3000000001;div_m=5.0;state=5;fix=1'),
        'M6: the dead body 5 m off is queued')
    ticks(bg, 6)
    check(logged(bg, 'event=place;seq=41;ent=3000000001;kind=correct;result=applied'), 'M6: placed (never terminal_local)')
    -- Enemy latch (the M6 ping-pong, run e583ee62): a dead guard reads NPC_STATE_DISABLE on both copies, so the other
    -- copy's body placement is refused here. This copy's body stays where it lies and is never reported back: no FIFO
    -- entry, no body_tx (there: 384 / 383 body_tx, one about every 1.1 s all session, for one dead guard).
    local bd = guestFix(180)
    bd.W.bodyOn = true
    bd.life[1] = 1
    bd.positions[1] = {0, 0.3, 20.0}
    bd.env.TppGameObject = {NPC_STATE_DISABLE = 9}; bd.status = {[1] = 9}
    ticks(bd, 60)
    bd.W.handlers[34]({42, ENT1, 3, 5000, 500, 20000, 0, 8, 0, 2}, bd.now)
    check(#bd.P.q == 0 and logged(bd, 'event=place_body;seq=42;ent=3000000001;div_m=5.0;state=5;fix=0;why=disabled_local'),
        'M6: a body placement this copy refuses (disabled_local) is not queued')
    ticks(bd, 330)
    check(not logged(bd, 'event=body_tx;'), 'M6: this copy\'s body is not reported back for 10 s (no ping-pong)')
    do
    -- Enemy gaps 2, body_dead (CFG r[19] bit 1024, W.deadOn; run 41f2b07f: a dead guard 6.8 m apart, refused
    -- disabled_local): the other copy's dead body placement goes through; after it the resting spot is where this
    -- copy's body really lies, so a corpse the game does not move is not reported back either (no ping-pong).
    local function deadFix(reply)
        local c = guestFix(180)
        c.W.bodyOn, c.W.deadOn = true, true
        c.life[1] = 1
        c.positions[1] = {0, 0.3, 20.0}
        c.env.TppGameObject = {NPC_STATE_DISABLE = 9}; c.status = {[1] = 9}
        c.warp = {reply}
        ticks(c, 60)
        return c
    end
    local dd = deadFix({r = 0, h = 1, leaf = 'typed'})
    dd.W.handlers[34]({43, ENT1, 3, 5000, 500, 20000, 0, 8, 0, 2}, dd.now)
    check(#dd.P.q == 1 and logged(dd, 'event=place_body;seq=43;ent=3000000001;div_m=5.0;state=5;fix=1;why=none'),
        'body_dead: the dead body 5 m off is queued (not disabled_local)')
    ticks(dd, 8)
    check(logged(dd, 'event=place;seq=43;ent=3000000001;kind=correct;result=applied'), 'body_dead: the dead body is placed')
    ticks(dd, 330)
    check(not logged(dd, 'event=body_tx;'), 'body_dead: a corpse that did not move is not reported back (rest = where it lies)')
    -- The corpse moved by the placement: no report either; a later carry of it is reported once.
    local dm = deadFix({r = 0, h = 1, leaf = 'typed'})
    dm.W.handlers[34]({44, ENT1, 3, 5000, 500, 20000, 0, 8, 0, 2}, dm.now)
    dm.positions[1] = {5.0, 0.3, 20.0}
    ticks(dm, 330)
    check(logged(dm, 'event=place;seq=44;ent=3000000001;kind=correct;result=applied') and not logged(dm, 'event=body_tx;'),
        'body_dead: a corpse the placement moved is not reported back')
    dm.positions[1] = {12.0, 0.3, 20.0}
    ticks(dm, 120)
    check(logCount(dm, 'event=body_tx;ent=3000000001') == 1, 'body_dead: a later carry of it is reported once')
    -- A dead body placement refused natively (relayed) leaves the resting spot where this body lies: no report.
    local dr = deadFix({refused = 'relayed'})
    dr.W.handlers[34]({45, ENT1, 3, 5000, 500, 20000, 0, 8, 0, 2}, dr.now)
    ticks(dr, 330)
    check(logged(dr, 'event=place;seq=45;ent=3000000001;kind=correct;result=refused;why=unrealized') and
        not logged(dr, 'event=body_tx;'), 'body_dead: a refused corpse placement is not reported back')
    -- Without body_dead (bodyOn only) the old refusal stands (the bd case above).
    -- Enemy gaps 2, place_reach (CFG r[19] bit 2048, W.reachOn; run 798b51dd: 57 host placements and 22 guest
    -- corrections refused unrealized, a dismounted rider 105-120 m off for 48 s): a correction the game relayed (not
    -- realized on this copy) force-realizes that guard for 8 s, so the next one lands; released when one lands.
    local rz = guestFix(180)
    rz.W.reachOn = true
    rz.positions[1] = {0, 1.8, 20.0}
    rz.warp = {{refused = 'relayed'}, {r = 0, h = 1, leaf = 'typed'}}
    sample(rz, 50, ENT1, 3000, 20000)
    ticks(rz, 3)
    check(logged(rz, 'event=place;seq=50;ent=3000000001;kind=correct;result=refused;why=unrealized;native=relayed') and
        sent(rz, 1, 'SetForceRealize') == 1 and logged(rz, 'event=realize_hold;ent=3000000001;on=1'),
        'place_reach: a relayed correction force-realizes that guard')
    ticks(rz, 40)
    sample(rz, 51, ENT1, 3000, 20000)
    ticks(rz, 8)
    check(logged(rz, 'event=place;seq=51;ent=3000000001;kind=correct;result=applied') and
        sent(rz, 1, 'SetForceRealize', false) == 1 and logged(rz, 'event=realize_hold;ent=3000000001;on=0;why=landed'),
        'place_reach: released once a placement of him lands')
    -- Not landed within 8 s: released (timeout); at most 4 at once; never without the key.
    local rt = guestFix(180)
    rt.W.reachOn = true
    rt.warp = {{refused = 'relayed'}}
    for i = 1, 5 do
        rt.positions[i] = {0, 1.8, 20.0 + i}
        sample(rt, 60 + i, 3000000000 + i, 3000, 20000 + i * 1000)
    end
    ticks(rt, 12)
    local holds = 0
    for i = 1, 5 do holds = holds + sent(rt, i, 'SetForceRealize') end
    check(holds == 4 and sent(rt, 5, 'SetForceRealize') == 0, 'place_reach: at most 4 guards held at once')
    ticks(rt, 250)
    check(sent(rt, 1, 'SetForceRealize', false) == 1 and logged(rt, 'event=realize_hold;ent=3000000001;on=0;why=timeout'),
        'place_reach: a hold ends after 8 s')
    local rn = guestFix(180)
    rn.positions[1] = {0, 1.8, 20.0}
    rn.warp = {{refused = 'relayed'}}
    sample(rn, 70, ENT1, 3000, 20000)
    ticks(rn, 3)
    check(logged(rn, 'event=place;seq=70;ent=3000000001;kind=correct;result=refused;why=unrealized') and
        sent(rn, 1, 'SetForceRealize') == 0, 'place_reach off: a relayed correction is only refused')
    -- The host's M3 placement (hplace) the same way.
    local rh = followFixture(true)
    rh.W.ownerOn, rh.W.reachOn = true, true
    rh.env.vars.playerCameraRotation = {[0] = 0, [1] = 0}
    rh.positions[900] = {100.0, 0, 0}
    rh.positions[2] = {95.0, 1.8, 0}
    rh.warp = {{refused = 'relayed'}}
    rh.W.handlers[71]({6, 3000000002, 3, 100000, 1400, 0, 0, 0, 0, 2}, rh.now)
    ticks(rh, 6)
    check(logged(rh, 'event=hplace;seq=6;ent=3000000002;kind=correct;result=refused;why=unrealized;native=relayed') and
        sent(rh, 2, 'SetForceRealize') == 1, 'place_reach: a relayed host placement force-realizes the host copy')
    -- The M4 stream reaches 160 m: the owner streams its guards within 160 m of the other player that stand within
    -- 60 m of its own; the receiver drives the other side's guards within 160 m of its player (native: 60 m of the nearer).
    local function snapsOf(c, slot)
        local n = 0
        for _, p in ipairs(placeTx(c, 75)) do if p.v[2] == slot then n = n + 1 end end
        return n
    end
    local rp = followFixture(true)
    rp.W.snapOn = true
    rp.positions[900] = {120.0, 0, 0}
    rp.positions[1] = {5.0, 1.8, 0}
    ticks(rp, 61)
    local far0 = snapsOf(rp, 1)
    rp.W.reachOn = true
    rp.tx = {}
    ticks(rp, 61)
    check(far0 == 0 and snapsOf(rp, 1) >= 3, 'place_reach: a guard 115 m from the other player (5 m from this one) is streamed')
    rp.positions[1] = {60.0, 1.8, 70.0}
    rp.tx = {}
    ticks(rp, 61)
    check(snapsOf(rp, 1) == 0, 'place_reach: not one 92 m from both players')
    local rd = guestFix(180)
    rd.W.snapOn = true
    rd.positions[1] = {0, 1.8, 100.0}
    ticks(rd, 2)
    local d0 = rd.drives and #rd.drives or 0
    rd.W.reachOn = true
    ticks(rd, 20)
    check(d0 == 0 and rd.drives and #rd.drives >= 1, 'place_reach: the receiver drives a guard 100 m away')
    -- Run 9023e5c6: most relays came from the stream's steps (host 18 drive / 3 placements, guest 22 / 1), and the
    -- native backoff after them refused the corrections (backoff 20 / 18): a relayed drive step holds the guard too.
    local rv = guestFix(180)
    rv.W.snapOn, rv.W.reachOn = true, true
    rv.positions[1] = {0, 1.8, 100.0}
    rv.driveReply = {refused = 'relayed'}
    ticks(rv, 2)
    check(sent(rv, 1, 'SetForceRealize') == 1 and logged(rv, 'event=realize_hold;ent=3000000001;on=1'),
        'place_reach: a relayed drive step force-realizes that guard')
    ticks(rv, 250)
    check(sent(rv, 1, 'SetForceRealize', false) == 1 and logged(rv, 'event=realize_hold;ent=3000000001;on=0;why=timeout'),
        'place_reach: a drive hold ends after 8 s')
    local rw = guestFix(180)
    rw.W.snapOn = true
    rw.positions[1] = {0, 1.8, 20.0}
    rw.driveReply = {refused = 'relayed'}
    ticks(rw, 2)
    check(sent(rw, 1, 'SetForceRealize') == 0, 'place_reach off: a relayed drive step holds nobody')
    check(logged(rv, ';src=drive;') and logged(rv, ';landed=0;timeout=1'),
        'place_reach: the release line names the source of the hold and counts the landed / timed-out holds')
    -- Enemy gaps 3 (design 7.18; run 3c9d1250: 14 host drive holds, every relay streak 1, all "timed out": a landed drive
    -- step never released a hold, and a backoff refusal never got one): a backoff drive step holds him too, his next step
    -- waits for the backoff's end (the native b), and a landed step releases him (why=landed, st=1).
    local rb = guestFix(180)
    rb.W.snapOn, rb.W.reachOn = true, true
    rb.positions[1] = {0, 1.8, 100.0}
    rb.driveReply = {refused = 'backoff', b = 1200}
    ticks(rb, 2)
    local nb = #rb.drives
    check(sent(rb, 1, 'SetForceRealize') == 1 and logged(rb, 'event=realize_hold;ent=3000000001;on=1;ok=1;n=1;why=backoff;b=1200'),
        'place_reach: a backoff drive step holds the guard')
    ticks(rb, 30)
    local nb2 = #rb.drives
    rb.driveReply = {e = 40, s = 1, r = 0, h = 1}
    ticks(rb, 10)
    check(nb == 1 and nb2 == nb and #rb.drives > nb2 and sent(rb, 1, 'SetForceRealize', false) == 1 and
        logged(rb, 'event=realize_hold;ent=3000000001;on=0;why=landed;') and logged(rb, ';src=drive;st=1;') and
        logged(rb, ';landed=1;timeout=0'), 'place_reach: the held drive step waits for the backoff end, lands and releases him')
    -- A correction refused in the backoff holds him too and is queued again when the backoff ends; it lands, releasing him.
    local rq = guestFix(180)
    rq.W.reachOn = true
    rq.positions[1] = {0, 1.8, 20.0}
    rq.warp = {{refused = 'backoff', b = 600}, {r = 0, h = 1, leaf = 'typed'}}
    sample(rq, 80, ENT1, 3000, 20000)
    ticks(rq, 3)
    check(logged(rq, 'event=place;seq=80;ent=3000000001;kind=correct;result=refused;why=unrealized;native=backoff') and
        logged(rq, 'event=realize_hold;ent=3000000001;on=1;ok=1;n=1;why=backoff;b=600'), 'place_reach: a backoff correction holds the guard')
    ticks(rq, 30)
    check(logged(rq, 'event=place;seq=80;ent=3000000001;kind=correct;result=applied') and
        logged(rq, 'event=realize_hold;ent=3000000001;on=0;why=landed;') and logged(rq, ';src=place;') and logged(rq, ';rq=1;'),
        'place_reach: the held correction is queued again at the backoff end and lands')
    -- A drive step refused while that correction waits keeps its retry (it used to clear it: P.hold set r.c = nil).
    local rr = guestFix(180)
    rr.W.snapOn, rr.W.reachOn = true, true
    rr.positions[1] = {0, 1.8, 20.0}
    rr.warp = {{refused = 'backoff', b = 600}, {r = 0, h = 1, leaf = 'typed'}}
    rr.driveReply = {refused = 'relayed', b = 3000}
    sample(rr, 81, ENT1, 3000, 20000)
    ticks(rr, 3)
    local relayedDrive = rr.drives and #rr.drives or 0
    ticks(rr, 110)
    check(relayedDrive >= 1 and logged(rr, 'event=place;seq=81;ent=3000000001;kind=correct;result=applied') and
        logged(rr, 'event=realize_hold;ent=3000000001;on=0;why=landed;') and logged(rr, ';rq=1;'),
        'place_reach: a relayed drive step keeps the held correction queued for the backoff end')
    -- Enemy round 7 track A (design 7.20; lua/coop/world_readback.lua) ------------------------------------------------
    do
    local t
    -- drive_sink counts the time under the target apart from snaps: round 6's 1 s drive_vert snap reset the 2 s window,
    -- so a copy more than 1.5 m under for at least 3 of about 4 s (025fbb28, 2942862796) logged none.
    t = guestFix(180)
    t.W.snapOn = true
    t.positions[1] = {0, 1.8, 20.0}
    t.driveReply = {e = 50, s = 0, r = 0, h = 1, v = -300}
    ticks(t, 30)
    t.driveReply = {e = 50, s = 0, r = 0, h = 1, v = 40}
    ticks(t, 15)
    t.driveReply = {e = 50, s = 0, r = 0, h = 1, v = -300}
    ticks(t, 36)
    check(logCount(t, 'event=drive_sink;ent=3000000001;') == 1, 'drive_sink: a snap that does not hold still counts (1 s + 1.2 s under)')
    t = guestFix(180)
    t.W.snapOn = true
    t.positions[1] = {0, 1.8, 20.0}
    t.driveReply = {e = 50, s = 0, r = 0, h = 1, v = -300}
    ticks(t, 30)
    t.driveReply = {e = 50, s = 0, r = 0, h = 1, v = 40}
    ticks(t, 66)
    t.driveReply = {e = 50, s = 0, r = 0, h = 1, v = -300}
    ticks(t, 36)
    check(not logged(t, 'event=drive_sink;'), 'drive_sink: level for 2 s starts the count over')
    -- Sparse readings (round 7 skeptic): a copy read once a second more than 1.5 m under logs drive_sink 2 s after the
    -- first low reading, as 0.15.1 did (a 500 ms cap on each gap made it 4 s).
    t = guestFix(180)
    t.W.snapOn = true
    t.positions[1] = {0, 1.8, 20.0}
    t.driveReply = {e = 50, s = 0, r = 0, h = 1, v = -300}
    ticks(t, 3, 1000)
    check(logCount(t, 'event=drive_sink;ent=3000000001;') == 1, 'drive_sink: readings 1 s apart count in full (2 s, as 0.15.1)')
    -- A y read-back 0.5 s and 2 s after a drive_vert snap (reply s=3; v: his y minus the target's before the snap, cm).
    t = guestFix(180)
    t.W.snapOn = true
    t.positions[1] = {0, 1.8, 20.0}
    t.driveReply = {e = 10, s = 3, r = 0, h = 1, v = -300}
    ticks(t, 1)
    t.driveReply = {e = 10, s = 0, r = 0, h = 1, v = 0}
    t.positions[1] = {0, 4.75, 20.0}
    ticks(t, 20)
    check(logged(t, 'event=readback;ent=3000000001;src=vert;at=500;') and logged(t, ';dy_m=-0.05;held=1;far_m=20'),
        'readback: 0.5 s after a vert snap, held on the target (dy within 1 m)')
    ticks(t, 50)
    check(logCount(t, 'event=readback;ent=3000000001;src=vert;') == 2 and logged(t, 'event=readback;ent=3000000001;src=vert;at=2000;'),
        'readback: and again 2 s after it, once each')
    t = guestFix(180)
    t.W.snapOn = true
    t.positions[1] = {0, 1.8, 20.0}
    t.driveReply = {e = 10, s = 3, r = 0, h = 1, v = -300}
    ticks(t, 1)
    t.driveReply = {e = 10, s = 0, r = 0, h = 1, v = -300}
    ticks(t, 70)
    check(logged(t, 'event=readback;ent=3000000001;src=vert;at=500;') and logged(t, ';dy_m=-3.00;held=0;far_m=20') and
        logged(t, 'event=readback;ent=3000000001;src=vert;at=2000;') and not logged(t, 'event=readback_fix;'),
        'readback: a snap that did not hold is logged held=0 with its dy; within 60 m nothing more')
    -- A held guard's landed drive step (it ends a place_reach hold) is read back too.
    t = guestFix(180)
    t.W.snapOn, t.W.reachOn = true, true
    t.positions[1] = {0, 1.8, 30.0}
    t.driveReply = {refused = 'backoff', b = 300}
    ticks(t, 2)
    t.driveReply = {e = 40, s = 1, r = 0, h = 1, v = -20}
    ticks(t, 40)
    check(logged(t, 'event=realize_hold;ent=3000000001;on=0;why=landed;') and
        logged(t, 'event=readback;ent=3000000001;src=drive;at=500;') and logged(t, ';dy_m=-0.20;held=1;'),
        'readback: the landed drive step that ends a hold')
    -- Far copies (place_reach; run 35d4074f: the host's copy of 1179373153 ~150 m off landed, was force-realized and
    -- dropped 3.7-5.4 m): a copy beyond 60 m more than 1.5 m under its target at a read-back is put back once, his
    -- force-realize ends and he is not held again for 10 s; falling again after that is only logged.
    t = guestFix(180)
    t.W.reachOn = true
    t.positions[1] = {0, 1.0, 100.0}
    t.warp = {{r = 0, h = 1, leaf = 'typed'}}
    sample(t, 90, ENT1, 3000, 100000)
    ticks(t, 6)
    check(logCount(t, 'event=place;seq=90;ent=3000000001;kind=correct;result=applied') == 1, 'far: the correction lands')
    t.positions[1] = {3.0, -2.5, 100.0}
    ticks(t, 20)
    check(logged(t, 'event=readback;ent=3000000001;src=place;at=500;') and logged(t, ';dy_m=-3.50;held=0;far_m=100') and
        logged(t, 'event=readback_fix;ent=3000000001;src=place;far_m=100;dy_m=-3.50;put=1'),
        'far: a copy 100 m off that fell 3.5 m is put back once')
    ticks(t, 10)
    check(logCount(t, 'event=place;seq=90;ent=3000000001;kind=correct;result=applied') == 2, 'far: the put-back lands')
    ticks(t, 70)
    check(logged(t, 'event=readback;ent=3000000001;src=put;at=500;') and
        logged(t, 'event=readback_fix;ent=3000000001;src=put;far_m=100;dy_m=-3.50;put=0;why=once') and
        logCount(t, 'event=place;seq=90;ent=3000000001;kind=correct;result=applied') == 2, 'far: falling again is not put back again')
    t.warp = {{refused = 'relayed'}}
    sample(t, 91, ENT1, 9000, 100000)
    ticks(t, 6)
    check(logged(t, 'event=place;seq=91;ent=3000000001;kind=correct;result=refused') and sent(t, 1, 'SetForceRealize') == 0 and
        logged(t, 'event=readback_nohold;ent=3000000001;'), 'far: no new force-realize hold for him within 10 s')
    -- The same fall within 60 m is not put back (only far copies), nor without place_reach.
    t = guestFix(180)
    t.positions[1] = {0, 1.0, 100.0}
    t.warp = {{r = 0, h = 1, leaf = 'typed'}}
    sample(t, 92, ENT1, 3000, 100000)
    ticks(t, 6)
    t.positions[1] = {3.0, -2.5, 100.0}
    ticks(t, 80)
    check(logged(t, 'event=readback;ent=3000000001;src=place;at=500;') and not logged(t, 'event=readback_fix;'),
        'far without place_reach: read back and logged only')
    end
    end
    -- A hold-up applied here raises the hands too (SetForceHoldup), once.
    local hu = guestFix(180)
    hu.W.bodyOn = true
    hu.W.handlers[16]({ENT1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0}, hu.now)
    for _ = 1, 4 do hu.now = hu.now + 33; hu.E.tick(hu.now) end
    local forced = 0
    for _, cmd in ipairs(hu.commands) do if cmd[2] == 'SetForceHoldup' then forced = forced + 1 end end
    check(forced == 1 and logged(hu, 'event=holdup_show;ent=3000000001;ok=1'), 'M6: the hold-up shows here')
    -- Enemy sync M5 (W.routeOn, docs/world-sync-design.md 7.11) ---------------------------------------------------------
    local function routeFix(host)
        local c = host and followFixture(true) or guestFix(180)
        c.W.routeOn = true
        c.env.mvars.ene_routeSetsDefine = {cp_a = {sneak_day = {{'rts_a', 'rts_b'}}}}
        c.W.byId = {}
        for i = 1, 5 do c.W.byId[i] = 3000000000 + i end
        return c
    end
    local rh = routeFix(true)
    rh.positions[900] = {10.0, 0, 0}
    rh.positions[1] = {5.0, 1.8, 0}
    rh.routeIds = {rts_a = {1}}
    ticks(rh, 2)
    local function routeTx(c)
        local out = {}
        for _, p in ipairs(placeTx(c, 34)) do if p.v[3] == 2 and p.v[8] == 16 then out[#out + 1] = p end end
        return out
    end
    local rtx = routeTx(rh)
    check(#rtx == 1 and rtx[1].v[2] == ENT1 and rtx[1].v[9] == rh.W.h('rts_a') and logged(rh, 'event=route_tx;ent=3000000001;route=rts_a'),
        'M5: the host sends the route of a guard near the guest (kind 2 flag 16)')
    ticks(rh, 4)
    check(#routeTx(rh) == 1, 'M5: once while it stays')
    rh.routeIds = {rts_b = {1}}
    ticks(rh, 4)
    rtx = routeTx(rh)
    check(#rtx == 2 and rtx[2].v[9] == rh.W.h('rts_b'), 'M5: a route change goes out')
    local rg = routeFix(false)
    rg.W.handlers[34]({50, ENT1, 2, 0, 0, 0, 0, 16, rg.W.h('rts_a'), 2}, rg.now)
    local applied = 0
    for _, cmd in ipairs(rg.commands) do if cmd[2] == 'SetSneakRoute' and cmd[5] == 'rts_a' then applied = applied + 1 end end
    check(applied == 1 and #rg.P.q == 0 and logged(rg, 'event=route_apply;ent=3000000001;route=rts_a;why=rx'),
        'M5: the guest copy takes the route (no placement)')
    rg.onRoute = {[1] = 'rts_a'}
    rg.W.handlers[34]({51, ENT1, 2, 0, 0, 0, 0, 16, rg.W.h('rts_a'), 2}, rg.now)
    applied = 0
    for _, cmd in ipairs(rg.commands) do if cmd[2] == 'SetSneakRoute' then applied = applied + 1 end end
    check(applied == 1, 'M5: already on it: nothing')
    rg.positions[1] = {0, 1.8, 20.0}
    rg.warp = {{r = 0, h = 1, leaf = 'engine'}}
    sample(rg, 52, ENT1, 1000, 20000)
    rg.now = rg.now + 2000
    sample(rg, 53, ENT1, 6000, 20000)
    ticks(rg, 6)
    check(logged(rg, 'event=route_apply;ent=3000000001;route=rts_a;why=landed'), 'M5: a landed correction re-joins the route')
    -- Enemy sync M7 probe (W.puppetOn): the guest's driven copies become puppets (SetPuppet, restrict notice) and take the
    -- puppet drive (DRIVE_V1 mode 1); outside 60 m they are set back.
    local pg = guestFix(180)
    pg.W.snapOn, pg.W.puppetOn = true, true
    pg.positions[1] = {0, 1.8, 20.0}
    ticks(pg, 2)
    local function cmdCount(c, name, on)
        local n = 0
        for _, cmd in ipairs(c.commands) do if cmd[2] == name then n = n + 1 end end
        return n
    end
    check(pg.drives and pg.drives[1] == "return 'MGS_COOP_WORLD_DRIVE_V1' -- 1 1 0 1800 20000 1" and cmdCount(pg, 'SetPuppet') == 1 and
        cmdCount(pg, 'SetRestrictNotice') == 1 and logged(pg, 'event=puppet;ent=3000000001;on=1;ok=1'), 'M7: a puppet, mode 1')
    ticks(pg, 4)
    check(cmdCount(pg, 'SetPuppet') == 1, 'M7: set once')
    pg.positions[1] = {0, 1.8, 90.0}
    ticks(pg, 2)
    check(cmdCount(pg, 'SetPuppet') == 2 and logged(pg, 'event=puppet;ent=3000000001;on=0'), 'M7: outside 60 m set back')
    -- Enemy sync M7 FM1, the far mirror (W.actOn; lua/coop/world_mirror.lua): a host-owned NORMAL guard beyond R + 15 m of
    -- the guest is a mirror puppet while his SOLDIER_ACT is fresh; within R, a guest shot (for 15 s), a stale stream, a
    -- research pause and the key off release him; the host registers its own guards beyond R - 10 m of the guest.
    do
    local mg = guestFix(0)
    mg.W.snapOn, mg.W.actOn, mg.W.mission = true, true, 30010
    mg.W.handlers[92]({1, 7})                               -- the partner's ACT_CAPS (round 6)
    -- R(ALERT) is 1.5 x the farthest ALERT hit (122.4 m on the host, run 3bc7280b) = 184 m, not capped: 12 copies stayed
    -- realized out to 212 m (host) and 400 m (guest), so no realize radius was measured (skeptic of 7b709720).
    -- FM1 retry (run 94a7bf2a): the guest's own copies hit it from up to 173.9 m in ALERT (guard 16), so R(ALERT) is
    -- 1.5 x 173.9 = 261 m by the plan's rule.
    check(mg.W.mirror and mg.W.mirror.R[0] == 80 and mg.W.mirror.R[2] == 261 and mg.W.mirror.R[3] == 261,
        'FM1: R 80 m calm, 261 m evasion / ALERT (1.5 x 173.9 m, 94a7bf2a)')
    mg.positions[1] = {0, 1.8, 100.0}
    ticks(mg, 4)
    local function acted(c, text)
        for _, s in ipairs(c.acts or {}) do if s == "return 'MGS_COOP_WORLD_ACT_V1' -- " .. text then return true end end
        return false
    end
    check(logged(mg, 'event=mirror;ent=3000000001;on=1;why=far;d_m=100.0;R=80') and acted(mg, 'q 1 1') and acted(mg, 'm 1 1') and
        cmdCount(mg, 'SetPuppet') == 1 and cmdCount(mg, 'SetRestrictNotice') == 1,
        'FM1: 100 m (beyond R + 15): queried (q), then a mirror puppet (m only once he is one)')
    -- Run 3bc7280b: 'm' for a mere candidate armed the native fire drop for guards whose own brain still ran (582 of their
    -- bullets dropped). A candidate is only queried; 'm' goes out only for puppets.
    local firstM
    for k, src in ipairs(mg.acts) do
        if src:find(' -- m ', 1, true) and not firstM then firstM = k end
    end
    local firstQ
    for k, src in ipairs(mg.acts) do
        if src:find(' -- q ', 1, true) and not firstQ then firstQ = k end
    end
    check(firstQ and firstM and firstQ < firstM, 'FM1: the first q comes before any m')
    mg.positions[1] = {0, 1.8, 85.0}
    ticks(mg, 4)
    check(not logged(mg, 'event=mirror;ent=3000000001;on=0'), 'FM1: between R and R + 15 m he stays a puppet')
    mg.positions[1] = {0, 1.8, 70.0}
    ticks(mg, 2)
    check(logged(mg, 'event=mirror;ent=3000000001;on=0;why=near;d_m=70.0;R=80') and acted(mg, 'r 1 1') and
        cmdCount(mg, 'SetPuppet') == 2, 'FM1: within R: released (his own brain again)')
    mg.positions[1] = {0, 1.8, 88.0}
    ticks(mg, 4)
    check(not logged(mg, 'event=mirror;ent=3000000001;on=1;why=far;d_m=88.0'), 'FM1: 88 m (under R + 15): not taken again')
    mg.positions[1] = {0, 1.8, 100.0}
    ticks(mg, 4)
    check(cmdCount(mg, 'SetPuppet') == 3, 'FM1: beyond R + 15 m again: a puppet again')
    mg.shots = 1
    ticks(mg, 2)
    check(logged(mg, 'event=mirror;ent=3000000001;on=0;why=shot'), 'FM1: the guest fired: released')
    ticks(mg, 8)
    check(cmdCount(mg, 'SetPuppet') == 4, 'FM1: no puppet again within 15 s of the shot')
    mg.now = mg.now + 15000
    ticks(mg, 4)
    check(cmdCount(mg, 'SetPuppet') == 5, 'FM1: 15 s after the shot: a puppet again')
    mg.actReply = {f = 0, sh = 1}
    ticks(mg, 4)
    check(not logged(mg, 'why=stale'), 'FM1: a missing act under 1.5 s keeps him')
    mg.now = mg.now + 1600
    ticks(mg, 2)
    check(logged(mg, 'event=mirror;ent=3000000001;on=0;why=stale'), 'FM1: no fresh SOLDIER_ACT for 1.5 s: released')
    mg.actReply = {f = 1, sh = 1}
    ticks(mg, 4)
    mg.actReply = {refused = 'paused', sh = 1}
    ticks(mg, 2)
    check(logged(mg, 'event=mirror;ent=3000000001;on=0;why=paused'), 'FM1: mirror off (native paused): released')
    mg.actReply = {f = 1, sh = 1}
    ticks(mg, 4)
    local before = cmdCount(mg, 'SetPuppet')
    mg.W.actOn = false
    ticks(mg, 1)
    check(logged(mg, 'why=key_off') and cmdCount(mg, 'SetPuppet') == before + 1, 'FM1: the key off releases every puppet')
    -- The host side: its own NORMAL guard within 250 m of the guest and beyond R - 10 m is registered; nearer ones not.
    local mh = followFixture(true)
    mh.W.snapOn, mh.W.actOn, mh.W.mission = true, true, 30010
    mh.W.handlers[92]({1, 7})
    mh.positions[900] = {0, 0, 0}
    mh.positions[1] = {0, 1.8, 100.0}
    mh.positions[2] = {0, 1.8, 30.0}
    ticks(mh, 4)
    check(acted(mh, 't 1 1') and not acted(mh, 't 2 2') and acted(mh, 's 0 0'), 'FM1 host: a guard beyond R - 10 m of the guest is sent')
    -- FM1 retry: the ALERT watch point is ~255 m from the fight (beyond R(ALERT) + 15 = 199 m), so the range is 300 m.
    check(mh.W.mirror.RANGE == 300, 'FM1: the mirror range is 300 m')
    -- Run 94a7bf2a: guard 19's host copy climbed a tower and his puppet stood at its foot 7 m off for ~100 s (nothing
    -- warps a copy under 15 m). A puppet > 5 m (3D) from his owner copy (the m reply's ox/oy/oz) for 2 s is released
    -- (why=apart: his own brain and the follow corrections again) and not taken again for 30 s.
    check((function()
        local ma = guestFix(0)
        ma.W.snapOn, ma.W.actOn, ma.W.mission = true, true, 30010
        ma.W.handlers[92]({1, 7})
        ma.positions[1] = {0, 1.8, 100.0}
        ma.actReply = {f = 1, sh = 0, ox = 0, oy = 1800, oz = 100000}
        ticks(ma, 70)
        if logged(ma, 'why=apart') or cmdCount(ma, 'SetPuppet') ~= 1 then return false end
        ma.actReply = {f = 1, sh = 0, ox = 0, oy = 8800, oz = 100000}
        ticks(ma, 20)
        if logged(ma, 'why=apart') then return false end  -- not before 2 s, nor in his first 4 s as a puppet
        ticks(ma, 160)  -- round 6: the owner copy still for 2 s first (here it jumped 7 m up), then 2 s apart
        if not (logged(ma, 'event=mirror_apart;ent=3000000001;apart_m=7.0') and logged(ma, 'on=0;why=apart') and acted(ma, 'r 1 1') and
            cmdCount(ma, 'SetPuppet') == 2) then return false end
        ma.actReply = {f = 1, sh = 0}
        ticks(ma, 300)
        if cmdCount(ma, 'SetPuppet') ~= 2 then return false end  -- held 30 s
        ticks(ma, 700)
        return cmdCount(ma, 'SetPuppet') == 3
    end)(), 'FM1: a puppet 7 m from his owner copy for 2 s is released (apart) and held off 30 s')
    check((function()  -- its own function: the main chunk is at Lua's 200-local limit
        local mr = followFixture(true)
        mr.W.snapOn, mr.W.actOn, mr.W.mission = true, true, 30010
        mr.W.handlers[92]({1, 7})
        mr.positions[900] = {0, 0, 0}
        mr.positions[1] = {0, 1.8, 280.0}
        mr.positions[2] = {0, 1.8, 320.0}
        ticks(mr, 4)
        return acted(mr, 't 1 1') and not acted(mr, 't 2 2')
    end)(), 'FM1 host: a guard 280 m from the guest is sent, one at 320 m not')
    -- Skeptic of FM1 (7b709720): a guest shot and a guest-local phase rise release the puppets within one update, not when
    -- the 12-per-update round robin reaches them (~0.8 s with ~280 roster entries); every way a puppet is dropped tells
    -- native (ACT_V1 r), so the fire-drop mask never outlives him; the local player's ACT_V1 p backs off 10 s while the
    -- hook is not installed (the defaults).
    local function actCount(c, text)
        local n = 0
        for _, s in ipairs(c.acts or {}) do if s == "return 'MGS_COOP_WORLD_ACT_V1' -- " .. text then n = n + 1 end end
        return n
    end
    local mr = guestFix(0)
    mr.W.snapOn, mr.W.actOn, mr.W.mission = true, true, 30010
    mr.W.handlers[92]({1, 7})
    mr.positions[1] = {0, 1.8, 100.0}
    ticks(mr, 4)
    check(mr.W.mirror.pp[3000000001] ~= nil, 'FM1 release: a puppet (setup)')
    mr.W.mirror.SCAN = 0                                   -- the round robin visits nobody
    mr.shots = 1
    ticks(mr, 1)
    check(logged(mr, 'event=mirror;ent=3000000001;on=0;why=shot') and mr.W.mirror.pp[3000000001] == nil,
        'FM1 release: a guest shot releases every puppet within one update')
    mr.W.mirror.SCAN = 12
    mr.now = mr.now + 16000
    ticks(mr, 4)
    check(mr.W.mirror.pp[3000000001] ~= nil, 'FM1 release: 15 s after the shot a puppet again')
    mr.W.mirror.SCAN = 0
    mr.W.cp[100].ownAt = mr.now + 1                         -- a guest-local phase rise in his CP
    ticks(mr, 1)
    check(logged(mr, 'event=mirror;ent=3000000001;on=0;why=phase') and mr.W.mirror.pp[3000000001] == nil,
        'FM1 release: a guest-local phase rise in his CP releases him within one update')
    mr.W.mirror.SCAN = 12
    mr.now = mr.now + 16000
    ticks(mr, 4)
    check(mr.W.mirror.pp[3000000001] ~= nil, 'FM1 release: a puppet again after the rise')
    mr.W.mirror.SCAN = 0
    local r0 = actCount(mr, 'r 1 1')
    mr.W.ent[3000000001].id = nil                           -- his game object is gone
    ticks(mr, 1)
    check(mr.W.mirror.pp[3000000001] == nil and actCount(mr, 'r 1 1') == r0 + 1,
        'FM1 release: a puppet whose object is gone is released natively (r), not only forgotten')
    mr.W.ent[3000000001].id = 1
    mr.W.mirror.SCAN = 12
    ticks(mr, 4)
    check(mr.W.mirror.pp[3000000001] ~= nil, 'FM1 release: a puppet again (setup)')
    local r1 = actCount(mr, 'r 1 1')
    for _, f in ipairs(mr.W.startHooks) do f() end
    check(next(mr.W.mirror.pp) == nil and actCount(mr, 'r 1 1') == r1 + 1,
        'FM1 release: a mission start releases the puppets natively (r) before forgetting them')
    local pb = guestFix(0)
    pb.pReply = {refused = 'hook'}
    local function pCount(c)
        local n = 0
        for _, s in ipairs(c.acts or {}) do if s:find(' -- p ', 1, true) then n = n + 1 end end
        return n
    end
    ticks(pb, 60)
    check(pCount(pb) == 1, 'FM1: ACT_V1 p refused (no hook, the defaults): not sent again for 10 s')
    pb.now = pb.now + 10000
    ticks(pb, 1)
    check(pCount(pb) == 2, 'FM1: ACT_V1 p offered again after 10 s')
    pb.pReply = nil
    pb.now = pb.now + 10000
    ticks(pb, 60)
    check(pCount(pb) >= 9, 'FM1: ACT_V1 p every 250 ms once the hook takes it')
    -- FM1 round 6 (docs/world-sync-design.md 7.19, protocol section 36): the far mirror runs only after the partner's
    -- ACT_CAPS (0x5C) with bit 1. A 0.15.0 partner has no op 0x5C and never sends one: nothing of soldier_act runs.
    ;(function()
        local function n(c, pat)
            local k = 0
            for _, s in ipairs(c.acts or {}) do if s:find(pat, 1, true) then k = k + 1 end end
            return k
        end
        local og = guestFix(0)
        og.W.snapOn, og.W.actOn, og.W.mission = true, true, 30010
        og.positions[1] = {0, 1.8, 100.0}
        ticks(og, 60)
        check(n(og, ' -- q ') == 0 and n(og, ' -- m ') == 0 and next(og.W.mirror.pp) == nil and cmdCount(og, 'SetPuppet') == 0,
            'FM1 round 6: an old partner (no ACT_CAPS): the guest asks nothing and takes no puppet')
        local oh = followFixture(true)
        oh.W.snapOn, oh.W.actOn, oh.W.mission = true, true, 30010
        oh.positions[900] = {0, 0, 0}
        oh.positions[1] = {0, 1.8, 100.0}
        ticks(oh, 60)
        check(n(oh, ' -- t ') == 0 and n(oh, ' -- s ') == 0, 'FM1 round 6: an old partner: the host registers and sends nothing')
        local capsTx = 0
        for _, t in ipairs(oh.tx) do if t.op == 92 and t.v[1] == 1 and t.v[2] == 7 then capsTx = capsTx + 1 end end
        check(capsTx == 1, 'FM1 round 12: a PC with soldier_act announces ACT_CAPS {1, 7} (revision 7) once while paired')
        -- Round 12 C (design 7.33, D-FM2): act_plan follows the gun profile with the far mirror, and its walk destination
        -- (the host copy's next path point) is revision 7's: a revision 6 partner (soldier_act named without act_plan, as
        -- 0.15.3 has it) takes and gives no puppet.
        og.W.handlers[92]({3, 6})
        ticks(og, 8)
        check(next(og.W.mirror.pp) == nil and n(og, ' -- m ') == 0 and logged(og, 'event=mirror_peer;caps=3;rev=6'),
            'FM1 round 12: a revision 6 partner (no act_plan) takes no puppet')
        oh.W.handlers[92]({3, 6})
        ticks(oh, 8)
        check(n(oh, ' -- t ') == 0 and n(oh, ' -- s ') == 0, 'FM1 round 12: a revision 6 partner: the host registers and sends nothing')
        og.W.handlers[92]({1, 7})
        ticks(og, 8)
        check(og.W.mirror.pp[3000000001] ~= nil and logged(og, 'event=mirror_peer;caps=1;rev=7'),
            'FM1 round 6: after the partner\'s ACT_CAPS: a puppet')
        oh.W.handlers[92]({1, 7})
        ticks(oh, 8)
        check(n(oh, ' -- t 1 1') >= 1 and n(oh, ' -- s ') >= 1, 'FM1 round 6: after the partner\'s ACT_CAPS: the host sends')
        og.W.handlers[92]({0, 6})
        ticks(og, 2)
        check(logged(og, 'on=0;why=peer') and next(og.W.mirror.pp) == nil, 'FM1 round 6: a partner without the mirror releases every puppet')
        og.W.handlers[92]({1, 7})
        for _, f in ipairs(og.W.unpairedHooks) do f(1) end
        check(og.W.mirror.peer == nil, 'FM1 round 6: unpaired forgets the partner\'s caps')
        local ow = guestFix(0)
        ow.W.snapOn, ow.W.mission = true, 30010
        ticks(ow, 60)
        local quiet = true
        for _, t in ipairs(ow.tx) do if t.op == 92 then quiet = false end end
        check(quiet, 'FM1 round 6: without soldier_act nothing is announced')
    end)()
    -- FM1 round 6 (4): the phase lever (soldier_act=calm, native's ACT_V1 p reply pl=1) mirrors only in calm / caution;
    -- without it (pl=0, the current behaviour) an ALERT puppet beyond R(ALERT) + 15 m stays.
    ;(function()
        local pl = guestFix(0)
        pl.W.snapOn, pl.W.actOn, pl.W.mission = true, true, 30010
        pl.W.handlers[92]({1, 7})
        pl.positions[1] = {0, 1.8, 280.0}
        ticks(pl, 8)
        check(pl.W.mirror.pp[3000000001] ~= nil, 'phase lever: a puppet at 280 m in calm (setup)')
        pl.W.cp[100].localPhase = 3
        ticks(pl, 40)
        check(pl.W.mirror.pp[3000000001] ~= nil and not logged(pl, 'why=phase_limit'), 'phase lever off (pl=0): an ALERT puppet at 280 m stays')
        pl.pReply = {p = 1, pl = 1}
        ticks(pl, 20)
        check(logged(pl, 'event=mirror;ent=3000000001;on=0;why=phase_limit') and pl.W.mirror.pp[3000000001] == nil,
            'phase lever pl=1: released in ALERT')
        ticks(pl, 40)
        check(pl.W.mirror.pp[3000000001] == nil, 'phase lever pl=1: none taken in ALERT')
        pl.W.cp[100].localPhase = 1
        ticks(pl, 40)
        check(pl.W.mirror.pp[3000000001] ~= nil, 'phase lever pl=1: a puppet again in caution')
        -- Round 12 C (D-FM1 (a); runs 516e3ef7 / 4735fee5: LRRP crew 176 / 177, their own CP calm, fought in CP 557's ALERT
        -- and the guest took them at 97-117 m): with the calm limit any CP at EVASION / ALERT on this PC ends the mirror.
        local limits = logCount(pl, 'event=mirror;ent=3000000001;on=0;why=phase_limit')
        pl.W.cp[100].localPhase = 0
        pl.W.cp[200].localPhase = 3
        ticks(pl, 20)
        check(logCount(pl, 'event=mirror;ent=3000000001;on=0;why=phase_limit') == limits + 1 and pl.W.mirror.pp[3000000001] == nil,
            'phase lever pl=1: released while another CP is in ALERT (D-FM1 (a))')
        ticks(pl, 40)
        check(pl.W.mirror.pp[3000000001] == nil, 'phase lever pl=1: none taken while another CP is in ALERT')
        pl.W.cp[200].localPhase = 0
        ticks(pl, 40)
        check(pl.W.mirror.pp[3000000001] ~= nil, 'phase lever pl=1: a puppet again once every CP is below EVASION')
    end)()
    -- FM1 round 6 (2): riders (GetVehicleGameObjectId) are skipped by the far rule (run adecad01: two LRRP riders of another
    -- CP were puppeted at 297 m and released apart 283-285 m off); a puppet who mounts is released (why=vehicle).
    ;(function()
        local rv = guestFix(0)
        rv.W.snapOn, rv.W.actOn, rv.W.mission = true, true, 30010
        rv.W.handlers[92]({1, 7})
        rv.vehicles = {[1] = 77}
        rv.positions[1] = {0, 1.8, 120.0}
        ticks(rv, 40)
        check(next(rv.W.mirror.pp) == nil, 'riders: a guard in a vehicle is never taken')
        rv.vehicles = {}
        ticks(rv, 40)
        check(rv.W.mirror.pp[3000000001] ~= nil, 'riders: on foot he is taken (setup)')
        rv.vehicles = {[1] = 77}
        ticks(rv, 40)
        check(logged(rv, 'event=mirror;ent=3000000001;on=0;why=vehicle') and rv.W.mirror.pp[3000000001] == nil,
            'riders: a puppet who mounts is released')
        local hv = followFixture(true)
        hv.W.snapOn, hv.W.actOn, hv.W.mission = true, true, 30010
        hv.W.handlers[92]({1, 7})
        hv.positions[900] = {0, 0, 0}
        hv.positions[1] = {0, 1.8, 100.0}
        hv.vehicles = {[1] = 77}
        ticks(hv, 8)
        local t = 0
        for _, s in ipairs(hv.acts or {}) do if s:find(' -- t 1 1', 1, true) then t = t + 1 end end
        check(t == 0, 'riders: the host does not send a rider')
    end)()
    -- FM1 round 6 (1): apart only for a still owner copy (the tower case); a walker lagging behind a moving owner copy is the
    -- catch-up's (native re-applies his request every 1 s), not apart's (runs adecad01 / 80cc0176: apart took them).
    ;(function()
        local mw = guestFix(0)
        mw.W.snapOn, mw.W.actOn, mw.W.mission = true, true, 30010
        mw.W.handlers[92]({1, 7})
        mw.positions[1] = {0, 1.8, 100.0}
        mw.actReply = {f = 1, sh = 0, ox = 0, oy = 1800, oz = 100000}
        ticks(mw, 70)
        local oz = 107000
        for _ = 1, 300 do
            oz = oz + 33
            mw.actReply = {f = 1, sh = 0, ox = 0, oy = 1800, oz = oz}
            mw.positions[1] = {0, 1.8, (oz - 7000) / 1000}
            ticks(mw, 1)
        end
        check(not logged(mw, 'why=apart') and mw.W.mirror.pp[3000000001] ~= nil,
            'apart: a walker 7 m behind a moving owner copy stays a puppet')
        ticks(mw, 200)
        check(logged(mw, 'on=0;why=apart'), 'apart: once the owner copy is still 2 s, 7 m off for 2 s releases him')
    end)()
    end
    -- M7 step 1 (W.perceiveOn): the host tells native soldier_perceive each sampled guard's nearer player (3 m band, a
    -- choice held 4 s); the guest never does.
    local pick = sh.W.snap.pick
    check(pick(nil, 400e6, 100e6, 0) == 1 and pick(nil, 100e6, 400e6, 0) == 0 and pick(nil, 400e6, nil, 0) == 0,
        'M7 perceive: the nearer player, the host when the guest is unknown')
    check(pick(1, (21000) ^ 2, (19000) ^ 2, 9000) == 1 and pick(0, (21000) ^ 2, (19000) ^ 2, 9000) == 0,
        'M7 perceive: within the 3 m band the choice stays')
    check(pick(1, (10000) ^ 2, (30000) ^ 2, 3000) == 1 and pick(1, (10000) ^ 2, (30000) ^ 2, 4000) == 0,
        'M7 perceive: a change waits until the choice is 4 s old')
    local function perceiveFix(host, on)
        local c = followFixture(host)
        c.W.perceiveOn = on
        local recv = c.W.receipt
        c.perceive = {}
        c.W.receipt = function(source)
            local text = source:match("^return 'MGS_COOP_SOLDIER_PERCEIVE_V1' %-%- (.*)$")
            if text then
                c.perceive[#c.perceive + 1] = text
                if text == 'status' then return 1, 1, 1, 1 end
                return 1
            end
            return recv(source)
        end
        c.positions[900] = {100.0, 0, 0}     -- the partner body
        c.positions[1] = {10.0, 1.8, 0}      -- 10 m from the host, 90 m from the partner
        c.positions[3] = {70.0, 1.8, 0}      -- 70 m from the host, 30 m from the partner
        return c
    end
    local function has(c, text) for _, t in ipairs(c.perceive) do if t == text then return true end end return false end
    local pv = perceiveFix(true, true)
    ticks(pv, 4)
    check(pv.perceive[1] == 'clear' and has(pv, '1 0') and has(pv, '3 1') and
        logged(pv, 'event=perceive;ent=3000000003;idx=3;target=1;dh_m=70;dq_m=30'), 'M7 perceive: the host sends each guard\'s player')
    local before = #pv.perceive
    ticks(pv, 40)
    check(not has(pv, '3 0') and #pv.perceive - before <= 1, 'M7 perceive: sent on a change only')
    pv.positions[3] = {20.0, 1.8, 0}
    ticks(pv, 400)
    check(has(pv, '3 0') and logged(pv, 'event=perceive;ent=3000000003;idx=3;target=0'), 'M7 perceive: back to the host when it is nearer')
    check(logged(pv, 'event=perceive_stats;native=1;t0=1;t1=1;auto=1;sent='), 'M7 perceive: the stats line')
    local pq = perceiveFix(false, true)
    ticks(pq, 40)
    check(#pq.perceive == 0, 'M7 perceive: never on the guest')
    local po = perceiveFix(true, false)
    ticks(po, 40)
    check(#po.perceive == 0, 'M7 perceive: off without the key')
    -- P13c (run dbba96de): told sight. The soldiers' own senses never pick up body 1, but a told discovery
    -- (SetDiscoveryPlayer) finds the offense player. One offense player for every guard: the player nearest to a sampled
    -- guard, held 4 s; a calm guard within 10 m of the guest for 0.8 s is told (offense 1 first, at most once per 30 s).
    local off = sh.W.snap.offense
    check(off(nil, 1, 0, false) == 1 and off(0, 1, 3000, false) == 0 and off(0, 1, 4000, false) == 1 and off(0, 1, 100, true) == 1 and
        off(1, 1, 0, false) == 1, 'M7 told sight: the offense player waits 4 s, a tell switches at once')
    check(has(pv, 'offense 0') and cmdCount(pv, 'SetDiscoveryPlayer') == 0 and not logged(pv, 'event=perceive_tell'),
        'M7 told sight: the host nearest to a guard is the offense player; nobody is told 30 m from the guest')
    local pt = perceiveFix(true, true)
    pt.positions[3] = {95.0, 1.8, 0}     -- 5 m from the partner body, 95 m from the host
    ticks(pt, 60)
    check(has(pt, '3 1') and has(pt, 'offense 1') and cmdCount(pt, 'SetDiscoveryPlayer') == 1 and
        logged(pt, 'event=perceive_tell;ent=3000000003;idx=3;dq_m=5.0') and logged(pt, 'event=perceive_offense;offense=1;why=tell'),
        'M7 told sight: a guard 5 m from the guest is told once, the offense player becomes body 1')
    ticks(pt, 300)
    check(cmdCount(pt, 'SetDiscoveryPlayer') == 1, 'M7 told sight: at most once per 30 s per guard')
    -- Run d7cb1b1d: the offense player flapped every 4.2-8.6 s with the guest 6-7 m from the guards and the host 15-20 m
    -- away, because the nearest guard was a 1 s rolling minimum that a host-side sample reset. Each guard keeps its own
    -- last sample; the nearest of the samples of the last 2 s decides.
    local nearest = sh.W.snap.nearest
    local nr = {[1] = {144e6, 0, 1000}, [3] = {25e6, 1, 0}}
    check(nearest(nr, 1500) == 1 and nearest(nr, 2001) == 0 and nearest(nr, 3001) == nil and nearest({}, 0) == nil,
        'M7 told sight: the nearest of every guard sampled in the last 2 s')
    local pa = perceiveFix(true, true)
    pa.positions[3] = {95.0, 1.8, 0}
    pa.W.cp[100].localPhase, pa.phase.cp_a = 3, 13  -- the guard's CP in ALERT
    ticks(pa, 60)
    check(cmdCount(pa, 'SetDiscoveryPlayer') == 0, 'M7 told sight: never a guard whose CP is in ALERT')
    local pg2 = perceiveFix(false, true)
    pg2.positions[3] = {95.0, 1.8, 0}
    ticks(pg2, 60)
    check(cmdCount(pg2, 'SetDiscoveryPlayer') == 0 and #pg2.perceive == 0, 'M7 told sight: never on the guest')
end
-- Mother Base (30050, feature allCp): no area trap ever sets W.activeCp, so every CP counts as loaded. The
-- snapshot reads each soldier (before: all unreadable, the guest refused it and W1 never came on), the guest
-- accepts and applies it, and a failed apply (a staff member on a platform this copy has not loaded) is retried
-- every 10 s, since no area change ever re-checks the ledger there.
do
    local mh, mg = fixture(true, 3, true), fixture(false, 3, true)
    check(mh.W.activeCp == nil and mh.W.inferredCp ~= nil and mh.W.inferredCp[100] and mh.W.inferredCp[200],
        'Mother Base: every CP counts as loaded for the snapshot reads')
    mh.life[2] = 3
    capture(mh)
    transfer(mh, mg)
    check(mg.W.w1 and logged(mg, 'step=accepted') and logged(mg, 'unknown=0'),
        'Mother Base: the guest accepts a fully readable snapshot (W1 on)')
    mg.W.enemy.tick(mg.now)
    mg.now = mg.now + 300; mg.W.enemy.tick(mg.now)
    check(mg.life[2] == 3, 'Mother Base: the snapshot puts the asleep staff member to sleep on the guest copy')
    mg.A.tick(mg.now)
    transfer(mg, mh, function(p) return p.op == OP.ALIGN_ACK end)
    check(mh.W.w1, 'Mother Base: the host resumes W1 after the ACK')
    mg.inert = true
    mg.W.enemy.apply(3000000004, 2, 5, 'outcome')
    for _ = 1, 80 do mg.now = mg.now + 33; mg.W.enemy.tick(mg.now) end
    local held = mg.W.enemy.s.ledger[3000000004]
    check(mg.life[4] == 0 and held ~= nil and held.reason == 'apply_failed', 'Mother Base: an apply that never lands is held')
    mg.inert = false
    for _ = 1, 400 do mg.now = mg.now + 33; mg.W.enemy.tick(mg.now) end
    check(mg.life[4] == 3 and mg.W.enemy.s.ledger[3000000004] == nil,
        'Mother Base: the held apply is retried within 10 s and lands once the platform is loaded')
end
-- Mother Base (feature spareStaff): stock f30050_sequence.lua reserves TARGET_DEAD (Mission Failed, PLAYER_KILL_DD)
-- on any non-player Dead or Dying. A guest's kill or fatal hit on a staff member is committed on the host but applied
-- to the host's staff member as a knockout: the host's own mission never fails for the guest's act.
do
    local sh, sg = fixture(true, 3, true), fixture(false, 3, true)
    capture(sh)
    transfer(sh, sg)
    sg.A.tick(sg.now)
    transfer(sg, sh, function(p) return p.op == OP.ALIGN_ACK end)
    check(sh.W.w1 and sg.W.w1, 'Mother Base: W1 on at both copies before the guest kill')
    local reserved, send = 0, sh.W.api.send
    sh.W.api.send = function(id, command)
        -- stub of f30050's GameObject Dead / Dying handler (DEAD = 1, DYING = 2 in this fixture)
        if command.id == 'ChangeLifeState' and (command.state == 1 or command.state == 2) then reserved = reserved + 1 end
        return send(id, command)
    end
    sh.W.handlers[OP.REQ_OUTCOME]({41, 3000000001, 1, 5, 0, 1, 0, 0, 0, 0, 0}, sh.now)
    sh.W.handlers[OP.REQ_OUTCOME]({42, 3000000002, 1, 4, 0, 4, 0, 0, 0, 0, 0}, sh.now)
    for _ = 1, 40 do sh.now = sh.now + 33; sh.W.enemy.tick(sh.now) end
    check(logged(sh, 'ent=3000000001;state=5;ver=2;actor=guest') and logged(sh, 'ent=3000000002;state=4;ver=2;actor=guest'),
        'Mother Base: the guest kill and dying requests are committed on the host')
    check(reserved == 0 and sh.life[1] == 4 and sh.life[2] == 4 and logged(sh, 'event=spare;ent=3000000001;state=5;as=3'),
        'Mother Base: the host copy knocks the staff member out and never reserves its own TARGET_DEAD game over')
end
do
    -- arrival-prevent (docs/world-sync-design.md 9.2): a guest still riding its arrival helicopter in free roam or Mother
    -- Base holds the host's checkpoint (0.14.5 saved it into a frozen helicopter, which every later Continue rebuilt) and
    -- applies it once off the helicopter (world_follow F.arrival.heli).
    for _, mb in ipairs({true, false}) do
        local ah, ag = fixture(true, 3, mb), fixture(false, 3, mb)
        pairReady(ah, ag)
        local riding = true
        ag.W.follow.arrival = {heli = function() return riding end, holdCheckpoint = true}
        ah.env.TppMission.UpdateCheckPoint({atCurrentPosition = true})
        ah.A.tick(ah.now); transfer(ah, ag, function(p) return p.op == OP.CHECKPOINT end)
        ag.A.tick(ag.now)
        check(ag.R.pending and not ag.saves and not ag.checkpointCalls and logCount(ag, 'step=checkpoint_wait;reason=arrival_heli;ver=1') == 1,
            'riding in: the shared checkpoint waits and nothing is saved (' .. ag.W.mission .. ')')
        ag.now = ag.now + 600; ag.A.tick(ag.now)
        check(ag.R.pending and not ag.saves and logCount(ag, 'reason=arrival_heli') == 1, 'still riding: still held, logged once')
        riding = false
        ag.now = ag.now + 600; ag.A.tick(ag.now)
        check(not ag.R.pending and ag.saves == 1 and ag.R.seen == 1 and logCount(ag, 'step=checkpoint;hash=0;ver=1;receipt=1') == 1,
            'off the helicopter: the held checkpoint applies and saves once')
    end
    -- Horse plan M1 (world_horse_share.lua): while anyone rides the shared D-Horse the guest's checkpoint waits too.
    do
        local hh, hg = fixture(true, 3), fixture(false, 3)
        pairReady(hh, hg)
        local busy = true
        hg.W.horseShare = {busy = function() return busy end}
        hh.env.TppMission.UpdateCheckPoint({atCurrentPosition = true})
        hh.A.tick(hh.now); transfer(hh, hg, function(p) return p.op == OP.CHECKPOINT end)
        hg.A.tick(hg.now)
        check(hg.R.pending and not hg.saves and logCount(hg, 'step=checkpoint_wait;reason=horse;ver=1') == 1,
            'someone rides the shared horse: the checkpoint waits, nothing saved')
        hg.now = hg.now + 600; hg.A.tick(hg.now)
        check(hg.R.pending and not hg.saves and logCount(hg, 'reason=horse') == 1, 'still riding: held, logged once')
        busy = false
        hg.now = hg.now + 600; hg.A.tick(hg.now)
        check(not hg.R.pending and hg.saves == 1, 'off the horse: the held checkpoint applies once')
    end
    -- Named off (F.arrival.holdCheckpoint = false; world_follow's default is on since 0.14.6): riding in applies at once.
    local default = fixture(false, 3, true)
    default.W.w1 = true
    default.W.follow.arrival = {heli = function() return true end, holdCheckpoint = false}
    default.W.handlers[OP.CHECKPOINT]({0, 1, 1100, 2200, 3300, 4400}); default.A.tick(default.now)
    check(default.R.seen == 1 and default.saves == 1 and logCount(default, 'arrival_heli') == 0, 'named off: the hold is off')
    -- In a helicopter space (the ACC) or without world_follow's way out, nothing is held.
    local acc = fixture(false)
    acc.W.mission, acc.W.w1 = 40050, true
    acc.W.follow.arrival = {heli = function() return true end, holdCheckpoint = true}
    acc.W.handlers[OP.CHECKPOINT]({0, 1, 1100, 2200, 3300, 4400}); acc.A.tick(acc.now)
    check(acc.R.seen == 1 and acc.saves == 1 and logCount(acc, 'arrival_heli') == 0, 'the ACC applies at once')
    local older = fixture(false)
    older.W.w1 = true
    older.W.handlers[OP.CHECKPOINT]({0, 1, 1100, 2200, 3300, 4400}); older.A.tick(older.now)
    check(older.R.seen == 1 and older.saves == 1, 'no F.arrival (older follow chunk): applies at once')
end
-- fb1031 (b): a host Game Over for a non-player reason (stock f30050 TARGET_DEAD: a staff member died) stays the host's
-- own, but the guest is told why it reloads: GAME_OVER {kind, radio, cause 4} -> world note 15 a=2 (b=1 a staff member
-- died at Mother Base, 0 another reason), repeated every 3 s until the reload (at most 60 s); never a guest Game Over.
do
    local function notes(c) c.notes = {}; c.W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end end
    local th, tg = fixture(true, 3, true), fixture(false, 3, true)
    for _, c in ipairs({th, tg}) do
        notes(c)
        c.env.TppDefine.GAME_OVER_TYPE.TARGET_DEAD, c.env.TppDefine.GAME_OVER_RADIO.PLAYER_KILL_DD = 5, 9
    end
    pairReady(th, tg)
    check(th.env.TppMission.ReserveGameOver(5, 9) == true and th.reserved == 1 and th.gameType == 5 and not th.R.dead and
        th.W.w1, 'host TARGET_DEAD: the stock reserve runs, no shared death, W1 stays on')
    check(logCount(th, 'step=host_game_over;type=5;cause=4') == 1, 'host logs its non-player game over')
    th.env.TppMission.ReserveGameOver(5, 9)
    th.A.tick(th.now)
    local sent = {}
    for _, p in ipairs(th.tx) do if p.op == OP.GAME_OVER then sent[#sent + 1] = p.v end end
    check(#sent == 1 and sent[1][1] == 5 and sent[1][2] == 9 and sent[1][3] == 4,
        'host sends GAME_OVER {kind, radio, cause 4} once (f30050 Dead and Dying both reserve)')
    transfer(th, tg, function(p) return p.op == OP.GAME_OVER end)
    check(not tg.reserved and not tg.R.dead and tg.A.dead ~= true, 'guest never reserves a non-player game over')
    check(#tg.notes == 1 and tg.notes[1][1] == 15 and tg.notes[1][2] == 2 and tg.notes[1][3] == 1,
        'guest: note 15 a=2 b=1 (a staff member died at Mother Base)')
    check(logCount(tg, 'step=partner_game_over;type=5;why=1') == 1, 'guest logs the partner game over')
    transfer(th, tg, function(p) return p.op == OP.GAME_OVER end)
    check(#tg.notes == 1, 'a duplicate broadcast adds no note')
    tg.now = tg.now + 2999; tg.A.tick(tg.now)
    check(#tg.notes == 1, 'no repeat before 3 s')
    tg.now = tg.now + 1; tg.A.tick(tg.now)
    check(#tg.notes == 2 and tg.notes[2][2] == 2, 'repeated every 3 s while the host is on its Game Over screen')
    tg.now = tg.now + 60000; tg.A.tick(tg.now)
    check(#tg.notes == 2, 'never repeated past 60 s')
    for _, f in ipairs(tg.W.startHooks) do f() end
    for _, f in ipairs(th.W.startHooks) do f() end
    check(not tg.R.ended and not th.R.ended, 'the reload clears both sides')
    -- Another non-player kind (or TARGET_DEAD outside Mother Base): b=0; an abort (aborting=true) is not a failure.
    local fh, fg = fixture(true), fixture(false)
    for _, c in ipairs({fh, fg}) do notes(c); c.env.TppDefine.GAME_OVER_TYPE.TARGET_DEAD = 5 end
    pairReady(fh, fg)
    fh.env.TppMission.ReserveGameOver(7, 3, true)
    fh.A.tick(fh.now)
    local aborts = 0
    for _, p in ipairs(fh.tx) do if p.op == OP.GAME_OVER then aborts = aborts + 1 end end
    check(aborts == 0 and fh.reserved == 1, 'an aborting reserve is not forwarded')
    for _, f in ipairs(fh.W.startHooks) do f() end
    fh.env.TppMission.ReserveGameOver(5, 3)
    fh.A.tick(fh.now)
    transfer(fh, fg, function(p) return p.op == OP.GAME_OVER end)
    check(#fg.notes == 1 and fg.notes[1][2] == 2 and fg.notes[1][3] == 0 and not fg.reserved, 'free roam: note 15 a=2 b=0')
    -- A refused host reserve sends nothing; a shared player death still shares as before (cause 1).
    local rh = fixture(true); notes(rh)
    rh.gameRefuse = true
    rh.env.TppMission.ReserveGameOver(5, 9); rh.A.tick(rh.now)
    local refused = 0
    for _, p in ipairs(rh.tx) do if p.op == OP.GAME_OVER then refused = refused + 1 end end
    check(refused == 0 and not rh.R.ended, 'a refused host reserve sends nothing')
    -- Lab probe host_target_dead (id 21, lab mode only): the host's stock TARGET_DEAD reserve, as f30050 does when a
    -- staff member dies, through the co-op wrapper.
    local ph, pg = fixture(true, 3, true), fixture(false, 3, true)
    for _, c in ipairs({ph, pg}) do
        c.env.TppDefine.GAME_OVER_TYPE.TARGET_DEAD, c.env.TppDefine.GAME_OVER_RADIO.PLAYER_KILL_DD = 5, 9
    end
    check(ph.W.probe.NAMES[21] == 'host_target_dead' and type(ph.W.probe.handlers.host_target_dead) == 'function',
        'probe 21 is host_target_dead')
    ph.W.probe.handlers.host_target_dead({name = 'host_target_dead'})
    check(ph.reserved == 1 and ph.gameType == 5 and ph.gameRadio == 9 and ph.R.ended and
        logCount(ph, 'event=probe;id=host_target_dead;step=done;result=ok') == 1, 'the probe reserves TARGET_DEAD on the host')
    pg.W.probe.handlers.host_target_dead({name = 'host_target_dead'})
    check(not pg.reserved and logCount(pg, 'event=probe;id=host_target_dead;step=done;result=refused') == 1, 'never on the guest')
end
print(checks .. ' W4 alignment/recovery and spawn sync checks passed; real apply rules, no game')
