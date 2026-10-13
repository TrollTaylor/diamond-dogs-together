-- Executes only the original downed policy against two in-memory game owners.
-- No game process, native call, window, save or system input is used.
local checks = 0
local function check(value, why) checks = checks + 1; assert(value, checks .. ': ' .. why) end
local OP = {DOWNED_STATUS = 36, DOWNED_COMMIT = 37, REVIVE_GRANT = 38, FRIENDLY_HIT = 41}
local function copy(t)
    local v = {}
    for k, item in pairs(t) do v[k] = type(item) == 'table' and copy(item) or item end
    return v
end
local function fixture(host)
    local c = {now = 0, tx = {}, nativeCalls = {}, logs = {}, announces = {}, hud = {},
        native = {ready = true, down = false, generation = 0, cause = 0, held = false, pos = {0, 0, 0}}}
    local env = setmetatable({e = {}, vars = {missionCode = 30010},
        Tpp = {MgsCoop = {playerPackageSelected = true}},
        PlayerInfo = {GetLocalPlayerIndex = function() return 0 end}, Player = {}}, {__index = _G})
    local W = {OP = OP, active = true, paired = true, eff = 3, role = host and 1 or 2,
        host = host, mission = 30010, epoch = 7, handlers = {}, startHooks = {}, stopHooks = {},
        pairedHooks = {}, unpairedHooks = {}, recovery = {dead = false}}
    W.get = function(t, k) return t and t[k] end
    W.now = function() return c.now end
    W.log = function(text) c.logs[#c.logs + 1] = text end
    W.inGame = function() return c.inGame ~= false end
    W.tx = function(op, ...)
        if c.failTx == op then return false end
        c.tx[#c.tx + 1] = {op = op, values = {...}}
        return true
    end
    W.receipt = function(text)
        if text == "return 'MGS_COOP_PARTNER_SLEEP_CAP_V1'" then return c.bridgeCap == true end
        if text:match("^return 'MGS_COOP_TEAM_V1'") then
            local tail = assert(text:match("%-%- (.*)$")); local v = {}
            for n in tail:gmatch("%d+") do v[#v + 1] = tonumber(n) end
            check(#v == 7 and v[1] == W.mission and v[2] == W.epoch, 'team receipt carries mission, epoch and five flags')
            check(v[3] <= 1 and v[4] <= 2 and v[5] <= 1 and v[6] <= 2 and v[7] <= 1 and
                (v[3] == 0) == (v[4] == 0) and (v[5] == 0) == (v[6] == 0), 'team receipt down flags agree with causes')
            c.team = v
            return true
        end
        if text:match("^return 'MGS_COOP_PARTNER_SLEEP_V1'") then
            local tail = assert(text:match("%-%- (.*)$")); local v = {}
            for n in tail:gmatch("%d+") do v[#v + 1] = tonumber(n) end
            check(#v == 9, 'partner sleep bridge uses nine finite integer fields')
            c.bridgePackets = c.bridgePackets or {}; c.bridgePackets[#c.bridgePackets + 1] = v
            return true
        end
        if text:match("^return 'MGS_COOP_DOWNED_HUD_V3'") then
            local m, ep, ld, td, inRange, progress, cause, teammateCause, near = text:match("%-%- (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+)$")
            check(m ~= nil, 'downed HUD V3 receipt uses nine bounded unsigned fields')
            local h = {mission = tonumber(m), epoch = tonumber(ep), down = tonumber(ld), teammateDown = tonumber(td),
                inRange = tonumber(inRange), progress = tonumber(progress), cause = tonumber(cause),
                teammateCause = tonumber(teammateCause), near = tonumber(near)}
            check(h.down <= 1 and h.teammateDown <= 1 and h.inRange <= 1 and h.near <= 1 and h.progress <= 3000 and
                h.cause <= 2 and h.teammateCause <= 2 and (h.down == 0) == (h.cause == 0) and
                (h.teammateDown == 0) == (h.teammateCause == 0), 'HUD incapacity causes agree with bounded down flags')
            check(h.near == h.inRange, 'unwrapped downed policy keeps the 3m approach equal to the hold range')
            check(h.progress == 0 or h.down == 0 and h.teammateDown == 1 and h.inRange == 1 and h.near == 1,
                'HUD cannot show progress for a downed, distant or absent reviver')
            if h.mission == 0 then check(h.epoch + h.down + h.teammateDown + h.inRange + h.near + h.progress + h.cause + h.teammateCause == 0, 'cleanup HUD is entirely zero')
            else check(h.mission == W.mission and h.epoch == W.epoch, 'HUD stays in its live mission and epoch') end
            c.hud[#c.hud + 1] = h; return true
        end
        if text:match("^return 'MGS_COOP_FRIENDLY_HIT_V1'") then
            local m, ep, cmd, seq, gen, dmg, cause = text:match("%-%- (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+)$")
            check(m == '30010' and ep == '7', 'friendly receipt retains native mission and epoch')
            cmd, seq, gen, dmg, cause = tonumber(cmd), tonumber(seq), tonumber(gen), tonumber(dmg), tonumber(cause)
            local n = c.native
            if c.failFriendly then return nil end
            local r = copy(n); r.applied = true
            if cmd == 0 then
                c.hitAcks = (c.hitAcks or 0) + 1
                if n.hitSequence == seq then n.hitSequence, n.hitDamageMilli, n.hitCause = nil, nil, nil end
                return r
            end
            if c.lastHit and seq <= c.lastHit then return r end
            c.lastHit = seq
            if n.generation ~= gen or n.down then return r end
            c.appliedHits = (c.appliedHits or 0) + 1
            n.life = n.life or 1000
            if cause == 2 or dmg / 1000 >= n.life then
                n.sleepLife = cause == 2 and n.life or nil
                n.down, n.cause, n.generation, n.life = true, cause == 2 and 2 or 1, n.generation + 1, cause == 2 and n.life or 50
            else n.life = n.life - dmg / 1000 end
            r = copy(n); r.applied = true; return r
        end
        local mission, epoch, command, generation = text:match("^return 'MGS_COOP_DOWNED_V1' %-%- (%d+) (%d+) (%d+) (%d+)$")
        check(mission == '30010' and epoch == '7', 'native receipt contains bound mission and epoch')
        command, generation = tonumber(command), tonumber(generation)
        c.nativeCalls[#c.nativeCalls + 1] = {command, generation}
        if c.refuseControl and command ~= 3 then return nil end
        if c.reply ~= nil then return c.reply end
        if c.throwNative then error('fixture native refusal') end
        if c.maskReleaseAt and c.now >= c.maskReleaseAt then c.mask, c.maskReleaseAt = false, nil end
        local n = c.native
        if command == 3 then
            c.cleanupAttempts = (c.cleanupAttempts or 0) + 1
            if c.throwCleanup then error('fixture cleanup refused') end
            if c.refuseCleanup then return {ready = false, applied = false} end
            if not c.cleanupAbsent then c.disarmed = (c.disarmed or 0) + 1 end
            n.down, n.cause = false, 0
            local r = copy(n); r.ready, r.applied = false, true; return r
        end
        if command == 5 then
            c.wakeAttempts = (c.wakeAttempts or 0) + 1
            check(c.mask == true, 'native sleep wake request retains named full input lock')
            local r = copy(n)
            r.applied, r.nativeSleepReleaseReady = c.nativeRetired == true, c.nativeRetired == true
            return r
        end
        if command == 2 then
            c.reviveAttempts = (c.reviveAttempts or 0) + 1
            if c.failRevive or not n.down or generation ~= n.generation then return nil end
            if c.mask then return copy(n) end
            c.revived = (c.revived or 0) + 1
            if n.cause == 2 and n.sleepLife then n.life = n.sleepLife end
            n.sleepLife, n.down, n.cause = nil, false, 0
            local r = copy(n); r.applied = true; return r
        end
        local r = copy(n)
        r.applied = command ~= 1 or (not c.failLock and c.mask == true)
        return r
    end
    W.recovery.failBothDowned = function()
        c.sharedAttempts = (c.sharedAttempts or 0) + 1
        if c.failShared then return false end
        c.sharedFailure, W.recovery.dead = (c.sharedFailure or 0) + 1, true
        return true
    end
    env.PlayerStance = {STAND = 'STAND'}
    env.Player.RequestToSetTargetStance = function(stance)
        check(stance == 'STAND', 'revive requests the stock stand transition')
        c.stance = stance
        c.stanceRequests = (c.stanceRequests or 0) + 1
    end
    env.Player.IsInstanceValid = function() return c.invalidPlayer ~= true end
    env.Player.SetPadMask = function(options)
        check(options.settingName == 'MgsCoopDowned' and options.except == false and
            options.buttons == (c.native.cause == 2 and 0xFFFFFFFF or 0xFDDFFFFF) and
            options.sticks == 0 and options.triggers == 3, 'cause-specific own mask: wounded crawl or immobile sleep, look preserved, gun triggers blocked')
        c.mask, c.maskSets, c.maskReleaseAt = true, (c.maskSets or 0) + 1, nil
    end
    env.Player.ResetPadMask = function(options)
        if c.failReset then error('queued mask reset refused') end
        check(options.settingName == 'MgsCoopDowned' and options.buttons == nil, 'only owned named mask reset')
        c.maskResets = (c.maskResets or 0) + 1
        if c.maskDelay then c.maskReleaseAt = c.now + c.maskDelay else c.mask = false end
    end
    env.TppUiCommand = {AnnounceLogView = function(text) c.announces[#c.announces + 1] = text end}
    env.e.MgsCoopWorld = W
    local chunk = assert(loadfile(arg[1])); setfenv(chunk, env); chunk()
    chunk = assert(loadfile(assert(arg[3], 'partner sleep adapter argument required'))); setfenv(chunk, env); chunk()
    check(type(W.downed.partnerSleepPublish) == 'function' and type(W.downed.partnerSleepClear) == 'function',
        'actual separate partner sleep adapter composes after downed owner')
    check(not c.bridgePackets and W.downed.partnerSleepBridge == nil,
        'adapter loading is passive and sends no capability, target or enable receipt')
    if arg[2] then chunk = assert(loadfile(arg[2])); setfenv(chunk, env); chunk() end
    c.W, c.D, c.env = W, W.downed, env
    return c
end
local function down(c, cause)
    c.native.down, c.native.cause = true, cause or 1
    c.native.generation = c.native.generation + 1
end
local function pump(a, b)
    local packets = a.tx; a.tx = {}
    for _, packet in ipairs(packets) do b.W.handlers[packet.op](copy(packet.values), b.now) end
    return packets
end
local function step(a, b, ms)
    a.now, b.now = a.now + ms, b.now + ms
    a.D.tick(a.now); b.D.tick(b.now)
    pump(a, b); pump(b, a)
end
local function pair()
    local a, b = fixture(true), fixture(false)
    step(a, b, 0)
    return a, b
end
local function status(seq, generation, isDown, cause, x, held, target, ready)
    return {seq, generation, isDown and 1 or 0, cause or 0, x or 0, 0, 0, held and 1 or 0, target or 0, ready == false and 0 or 1}
end

-- Missing native support is inert: the Lua module cannot synthesize a protected
-- player or turn a vanilla death into a permanent unusable state.
for _, reply in ipairs({'MGS_COOP_DOWNED_V1', false, {}, {ready = false},
    {ready = true, down = false, held = false, generation = 0, cause = 0, pos = {0/0, 0, 0}}}) do
    local c = fixture(true); c.reply = reply; c.D.tick(0)
    check(#c.tx == 0 and c.D.handleDeath(0) == false and not c.D.active, 'missing or malformed capability preserves ordinary death')
end
local c = fixture(true); c.throwNative = true; c.D.tick(0)
check(not c.D.active and c.D.handleDeath(0) == false, 'native exception fails closed without disrupting original handler')
for _, field in ipairs({'paired', 'active'}) do
    c = fixture(true); c.W[field] = false; c.D.tick(0)
    check(#c.nativeCalls == 0 and c.D.handleDeath(0) == false, 'no writes outside paired active world: ' .. field)
end
for _, mission in ipairs({10010, 30011, 40010, 40020, 40050}) do
    c = fixture(true); c.W.mission, c.env.vars.missionCode = mission, mission; c.D.tick(0)
    check(#c.nativeCalls == 0 and #c.tx == 0, 'unsupported mission and ACC are unchanged')
end
c = fixture(true); c.env.Tpp.MgsCoop.playerPackageSelected = false; c.D.tick(0)
check(#c.nativeCalls == 0, 'qualified paired player package required')
c = fixture(true); c.W.eff = 2; c.D.tick(0)
check(#c.nativeCalls == 0, 'effective world level 3 required')
c = fixture(true); c.inGame = false; c.D.tick(0)
check(#c.nativeCalls == 0, 'non-gameplay does not arm or write')

-- HUD receipts use fresh owner facts and never retain misleading old holds.
do
    local a, b = pair()
    down(b); a.native.held = true
    for _ = 1, 18 do step(a, b, 100) end
    local h = a.hud[#a.hud]
    check(h.mission == 30010 and h.epoch == 7 and h.down == 0 and h.teammateDown == 1 and
        h.inRange == 1 and h.progress > 1000, 'live owner publishes actual in-range revive hold progress')
    check(b.hud[#b.hud].down == 1 and b.hud[#b.hud].progress == 0, 'downed owner waits without reviver progress')
    a.native.held = false; step(a, b, 100)
    check(a.hud[#a.hud].inRange == 1 and a.hud[#a.hud].progress == 0, 'release clears progress while retaining approach/action guidance')
    a.native.pos[1] = 3001; step(a, b, 100)
    check(a.hud[#a.hud].inRange == 0 and a.hud[#a.hud].progress == 0, 'outside range clears HUD progress immediately')
    local before = #a.hud; a.refuseControl = true; a.D.tick(a.now + 100)
    check(#a.hud == before + 1 and a.hud[#a.hud].mission == 0, 'failed native poll clears rather than advertising a stale live HUD')
    a.refuseControl = nil; a.D.stop()
    check(a.hud[#a.hud].mission == 0 and a.hud[#a.hud].epoch == 0, 'stop clears copied HUD ownership')
    a.D.reset(); check(a.hud[#a.hud].mission == 0, 'mission reset clears copied HUD ownership')
    a, b = pair(); down(a); b.native.held = true
    for _ = 1, 18 do step(a, b, 100) end
    h = b.D.hud(b.now)
    check(h.inRange and h.progress > 0, 'guest HUD consumes current host-authorized hold progress')
    b.D.remoteCommitAt = b.now - 501
    check(b.D.hud(b.now).progress == 0, 'stale host commit cannot retain revive progress')
    b.D.remoteCommitAt = b.now + 1
    check(b.D.hud(b.now).progress == 0, 'future host commit cannot grant HUD progress')
    b.D.remoteCommitAt = b.now; b.D.remoteCommit[9] = b.W.role
    check(b.D.hud(b.now).progress == 0, 'commit for a different target cannot advertise local revive progress')
    b.D.remoteCommit[9] = 1
    b.D.remoteCommit[2] = b.D.records[1].generation + 1
    check(b.D.hud(b.now).progress == 0, 'old target cycle commit cannot advertise progress for a different incapacity')
    b.D.remoteCommit[2] = b.D.records[1].generation
    b.D.remoteCommit[5] = b.D.records[2].generation + 1
    check(b.D.hud(b.now).progress == 0, 'old reviver cycle commit cannot advertise progress for a different live actor')
    b.D.records[1].at = b.now - 501
    h = b.D.hud(b.now)
    check(not h.teammateDown and not h.inRange and h.progress == 0, 'stale teammate status cannot advertise downed/range/progress facts')
end

-- Both lethal and teammate sleep use the same bounded state and never bleed out.
for _, cause in ipairs({1, 2}) do
    local a, b = pair(); down(b, cause); step(a, b, 100)
    check(b.D.records[2].down and a.D.records[2].down and b.D.records[2].cause == cause,
        'lethal/sleep cause publishes owned downed state')
    check(b.D.handleDeath(0) == true and b.D.handleDeath(1) == false, 'only protected LOCAL death can be suppressed')
    for _ = 1, 70 do step(a, b, 100) end
    check(not b.revived and not a.sharedFailure and b.D.records[2].down, 'one downed player waits indefinitely without auto-revive or bleedout')
    check(#b.announces == 0 and b.hud[#b.hud].down == 1, 'actual copied HUD carries downed guidance without an unverified localization call')
end

-- Range is a real 3D distance, not just a floor-plane approximation.
local a, b = pair(); down(b); b.native.pos = {3000, 0, 0}; a.native.held = true
for _ = 1, 33 do step(a, b, 100) end
check(b.revived == 1 and not b.native.down and not a.sharedFailure, 'exactly 3 m, continuous action hold revives guest')
check(b.stance == 'STAND' and b.stanceRequests == 1, 'the revived player requests the stock stand transition once')
check(a.D.granted[2] == 1 and b.D.receivedGrant > 0, 'host grant records exact downed generation')
step(a, b, 100)
check(not a.D.records[2].down and not b.D.records[2].down, 'normal status accepted only following matching grant')
a, b = pair(); down(b); b.native.pos = {0, 3001, 0}; a.native.held = true
for _ = 1, 40 do step(a, b, 100) end
check(not b.revived and a.D.progress == 0, 'vertical distance outside 3 m refuses revive')
a, b = pair(); down(b); b.native.pos = {3001, 0, 0}; a.native.held = true
for _ = 1, 40 do step(a, b, 100) end
check(not b.revived, 'just outside range refuses revive')

-- The host can be revived by the guest, and 2.9 s is not enough.
a, b = pair(); down(a, 2); b.native.held = true
for _ = 1, 30 do step(a, b, 100) end
check(not a.revived and a.D.progress < 3000, 'a partial hold never completes early')
for _ = 1, 3 do step(a, b, 100) end
check(a.revived == 1 and not a.native.down, 'guest can revive host after validated full hold')
for _ = 1, 3 do step(a, b, 100) end
check(not b.D.records[1].down and b.D.granted[1] == a.native.generation and not b.revived,
    'host native revive becomes guest observation permission without reviving guest owner')
a, b = pair(); down(a); b.native.held = true; a.failTx = OP.DOWNED_COMMIT
for _ = 1, 40 do step(a, b, 100) end
check(a.revived == 1 and b.D.records[1].down and not b.D.granted[1],
    'host native revive survives full commit queue while guest waits for authenticated observation')
a.failTx = nil
for _ = 1, 3 do step(a, b, 100) end
check(a.revived == 1 and not b.D.records[1].down and not b.revived,
    'commit retry permits next host status after an earlier alive status was reordered and refused')
local c = fixture(false)
c.D.acceptStatus(status(1, 4, true, 1), 1, 0)
check(not c.D.acceptStatus(status(2, 4, false, 0), 1, 100), 'alive host status before commit cannot self-authorize observation')
c.W.handlers[OP.DOWNED_COMMIT]({1, 3, 0, 0, 0, 0, 0, 0, 0}, 100)
check(not c.D.granted[1], 'wrong host generation commit cannot permit the current downed-to-live observation')
c.W.handlers[OP.DOWNED_COMMIT]({2, 4, 0, 0, 0, 0, 0, 0, 0}, 200)
check(c.D.granted[1] == 4 and c.D.acceptStatus(status(3, 4, false, 0), 1, 300) and not c.revived,
    'newer exact host-alive commit permits remote readback without a guest native revive call')

-- A retail healthy-owner rebind advances its native generation without a
-- mission transition. Execute both actual Lua owners across that barrier: old
-- partial E time cannot carry over, fresh status and a new complete hold can.
for _, downHost in ipairs({false, true}) do
    local host, guest = pair()
    local target, actor = downHost and host or guest, downHost and guest or host
    down(target, 2); actor.native.held = true
    for _ = 1, 18 do step(host, guest, 100) end
    check(host.D.progress > 1000 and not target.revived, 'partial hold established before healthy native owner rebind')
    local beforeGeneration = actor.native.generation
    actor.native.generation = beforeGeneration + 1 -- exact native NewScene barrier
    step(host, guest, 100); step(host, guest, 100)
    check(actor.D.records[actor.W.role].generation == beforeGeneration + 1 and
        host.D.records[actor.W.role].generation == beforeGeneration + 1,
        'healthy rebind generation publishes locally and is accepted by the real peer policy')
    check(host.D.progress <= 100 and not target.revived,
        'healthy actor ownership change restarts the uninterrupted hold rather than finishing old progress')
    for _ = 1, 27 do step(host, guest, 100) end
    check(not target.revived, 'fresh binding cannot reuse the previous partial hold to revive early')
    for _ = 1, 5 do step(host, guest, 100) end
    check(target.revived == 1 and not target.native.down,
        'either healthy player can still complete a fresh revive after exact native rearm')
end

-- Release, moving out of range, losing native protection, a time reversal and
-- a paused owner all reset elapsed time rather than adding separated holds.
for _, interruption in ipairs({'release', 'range', 'stale', 'pause', 'time', 'native'}) do
    a, b = pair(); down(b); a.native.held = true
    for _ = 1, 20 do step(a, b, 100) end
    check(a.D.progress > 0 and not b.revived, 'partial progress before ' .. interruption)
    if interruption == 'release' then a.native.held = false; step(a, b, 100); a.native.held = true
    elseif interruption == 'range' then a.native.pos[1] = 4000; step(a, b, 100); a.native.pos[1] = 0
    elseif interruption == 'stale' then
        a.now = a.now + 600; a.D.tick(a.now); a.tx = {}; b.now = a.now
    elseif interruption == 'pause' then a.inGame = false; step(a, b, 100); a.inGame = true
    elseif interruption == 'time' then a.now, b.now = a.now - 200, b.now - 200; step(a, b, 0)
    else a.failLock = true; down(a); step(a, b, 100); a.failLock = false; a.native.down, a.native.cause = false, 0 end
    check(a.D.progress == 0 and not b.revived, 'interruption resets hold: ' .. interruption)
    if interruption ~= 'native' then
        for _ = 1, 14 do step(a, b, 100) end
        check(not b.revived, 'separated holds cannot accumulate: ' .. interruption)
    end
end

-- Wire evidence cannot be refreshed by a duplicate or stale sequence, and
-- observing a normal body cannot self-authorize a revive.
c = fixture(true)
check(c.D.acceptStatus(status(1, 1, true, 1), 2, 100) == true, 'first downed packet accepted')
check(c.D.acceptStatus(status(1, 1, true, 1, 0, true, 1), 2, 400) == false and c.D.records[2].at == 100,
    'duplicate cannot renew liveness or held input')
check(c.D.acceptStatus(status(65535, 1, true, 1), 2, 500) == false, 'stale wrapping sequence rejected')
check(c.D.acceptStatus(status(2, 1, false, 0), 2, 600) == false and c.D.records[2].down,
    'self-revive status refused without host grant')
check(c.D.acceptStatus(status(2, 0, true, 1), 2, 600) == false, 'downed generation zero refused')
check(c.D.acceptStatus(status(2, 0, false, 0), 2, 600) == false, 'generation regression refused')
check(c.D.acceptStatus(status(2, 2, true, 2), 2, 600), 'new downed generation can replace older down')
c.D.granted[2] = 1
check(not c.D.acceptStatus(status(3, 2, false, 0), 2, 700), 'old grant cannot unlock new downed generation')
c.D.granted[2] = 2
check(c.D.acceptStatus(status(3, 2, false, 0), 2, 700), 'matching granted generation allows normal readback')
c = fixture(true)
check(c.D.acceptStatus(status(65535, 0, false), 2, 0) and c.D.acceptStatus(status(1, 0, false), 2, 1),
    'sequence wrap 65535 to 1 remains ordered')
for _, mutate in ipairs({function(v) v[1] = 0 end, function(v) v[3] = 2 end,
    function(v) v[4] = 3 end, function(v) v[5] = 1000000001 end,
    function(v) v[6] = 0/0 end, function(v) v[8] = 2 end, function(v) v[9] = -1 end,
    function(v) v[10] = 2 end, function(v) v[11] = 1 end,
    function(v) v[3], v[4], v[10] = 1, 1, 0 end}) do
    c = fixture(true); local v = status(1, 1, false); mutate(v)
    check(not c.D.acceptStatus(v, 2, 0) and not c.D.records[2], 'malformed packet leaves record unchanged')
end

-- Guest commit packets cannot clear the local native latch. Host-only exact
-- generation grants are the only transition; stale/foreign grants are inert.
a, b = pair(); down(b); step(a, b, 100)
local grant = {2, 1, 1}
b.W.handlers[OP.DOWNED_COMMIT]({1, 0, 0, 0, 1, 0, 0, 0, 0})
check(b.native.down and not b.revived, 'host HUD commit is not a health/control write')
b.W.handlers[OP.REVIVE_GRANT]({1, 1, 1}); b.W.handlers[OP.REVIVE_GRANT]({2, 2, 1})
check(not b.revived, 'foreign role and generation grants refused')
b.W.handlers[OP.REVIVE_GRANT](grant)
check(b.revived == 1 and b.D.receivedGrant == 1, 'exact host grant applied once')
b.W.handlers[OP.REVIVE_GRANT](grant)
check(b.revived == 1, 'duplicate grant does not heal or unlock twice')
a.W.handlers[OP.REVIVE_GRANT]({1, 1, 1})
check(not a.revived, 'host ignores guest attempt to send a host grant')

-- A refused native revive keeps the body protected, and the same legitimate
-- grant retries until native acknowledgement without restarting the hold.
a, b = pair(); down(b); step(a, b, 100); b.failRevive = true
b.W.handlers[OP.REVIVE_GRANT]({2, 1, 10})
check(b.native.down and b.D.pendingGrant and b.D.receivedGrant == 0, 'native refusal keeps downed state and pending grant')
b.failRevive = false; b.now = 200; b.D.tick(200)
check(b.revived == 1 and b.D.receivedGrant == 10 and not b.D.pendingGrant, 'native accepted retry finishes one grant')
a, b = pair(); down(b); a.native.held = true; a.failTx = OP.REVIVE_GRANT
for _ = 1, 40 do step(a, b, 100) end
check(not b.revived and not a.D.granted[2], 'failed send cannot permit unobserved normal status')
a.failTx = nil; step(a, b, 100)
check(b.revived == 1, 'failed host grant send retries after full valid hold')

-- The named pad mask is a queued engine update. Once the full hold has been
-- authorized, either owner completes that exact transaction after readback,
-- even if the reviver releases or moves away during the queued reset.
for _, targetHost in ipairs({true, false}) do
    a, b = pair()
    local target, actor = targetHost and a or b, targetHost and b or a
    down(target); target.maskDelay = 300; actor.native.held = true
    for _ = 1, 33 do step(a, b, 100) end
    check(not target.revived and target.D.unlockGeneration == 1 and target.native.down,
        'queued mask release retains downed protection until confirmed')
    check(a.D.pendingGrant ~= nil, 'completed host hold becomes a pending exact-generation transaction')
    actor.native.held, actor.native.pos = false, {4000, 0, 0}
    for _ = 1, 4 do step(a, b, 100) end
    check(target.revived == 1 and not target.native.down and not target.mask,
        'completed hold survives action release and movement while mask reset settles')
    check(not target.D.unlockGeneration and not a.D.pendingGrant,
        'asynchronous completion clears local unlock and host transaction')
end
-- Simultaneous incapacitation uses shared checkpoint failure, not an automatic
-- revival or independent game over. An unavailable helper does not invent one.
a, b = pair(); down(a); down(b); step(a, b, 100); step(a, b, 100)
check(a.sharedFailure == 1 and a.W.recovery.dead and not a.revived and not b.revived,
    'both protected players down invokes normal shared recovery once')
for _ = 1, 4 do step(a, b, 100) end
check(a.sharedFailure == 1, 'duplicate down observations cannot recommit failure')
a, b = pair(); down(a); down(b); a.failShared = true
step(a, b, 100); step(a, b, 100)
check(not a.sharedFailure and a.sharedAttempts > 0, 'rejected shared game over remains pending')
a.failShared = false; step(a, b, 100)
check(a.sharedFailure == 1, 'shared failure retries through existing recovery authority')

-- Mission/disconnect cleanup removes owned controls exactly once; enemy-only
-- world synchronization disabling must not release a downed player's mask.
c = fixture(true); down(c); c.D.tick(0)
c.W.stopHooks[1](false)
check(c.D.active and not c.disarmed, 'enemy-only stop preserves downed state')
c.W.unpairedHooks[1]()
check(c.disarmed == 1 and not c.D.active and not next(c.D.records), 'disconnect disarms and clears bounded policy state')
c.W.stopHooks[1](true)
check(c.disarmed == 1, 'repeated cleanup does not duplicate native restoration')
c = fixture(true); down(c); c.D.tick(0); c.W.pairedHooks[1](true)
check(c.disarmed == 1 and not c.D.active, 'new paired epoch disarms old generation')
c = fixture(true); down(c); c.failLock = true; c.D.tick(0)
check(not c.D.records[1] and #c.tx == 0, 'unconfirmed prone/control lock is never advertised as safely downed')
-- A native control failure is bounded and removes the owned Lua mask. A brief
-- unavailable snapshot is retried without letting the downed body stand.
c = fixture(true); down(c); c.D.tick(0); c.refuseControl = true
for i = 1, 22 do c.now = i * 100; c.D.tick(c.now) end
check(not c.mask and c.maskResets == 1 and c.disarmed == 1 and not c.D.active,
    'persistent native refusal releases named mask and restores owned native state')
c = fixture(true); down(c); c.D.tick(0); c.refuseControl = true
for i = 1, 5 do c.now = i * 100; c.D.tick(c.now) end
check(c.mask and not c.disarmed and c.D.active, 'brief native snapshot refusal preserves downed lock')
c.refuseControl = false; c.now = 600; c.D.tick(600)
check(c.mask and c.D.records[1].down and c.D.records[1].ready, 'temporary refusal recovers protected status')
c = fixture(true); down(c); c.D.tick(0); c.failReset = true
c.W.unpairedHooks[1]()
check(c.mask and c.D.maskOwned and not c.D.active and c.disarmed == 1,
    'failed named-mask reset retains cleanup ownership after native restoration')
c.failReset = false; c.W.paired = false; c.now = 100; c.D.tick(100)
check(not c.mask and not c.D.maskOwned and c.maskResets == 1,
    'next natural tick retries only owned cleanup even when no longer paired')
-- Cleanup acknowledgment is applied=true and intentionally ready=false.
-- Refusal cannot forget native ownership after the named mask is released.
c = fixture(true); down(c); c.D.tick(0); c.refuseCleanup = true
c.W.paired = false; c.W.unpairedHooks[1]()
check(c.D.nativePending and c.D.boundMission == 30010 and c.D.boundEpoch == 7 and not c.D.active,
    'refused disarm retains exact old owner binding independently of active gameplay')
check(not c.mask and not c.D.maskOwned and not c.disarmed and c.cleanupAttempts == 1,
    'named mask may release while native disarm independently remains pending')
c.W.epoch = 42; c.now = 100; local calls = #c.nativeCalls; c.D.tick(100)
check(c.D.nativePending and c.cleanupAttempts == 2 and #c.nativeCalls == calls + 1 and c.nativeCalls[#c.nativeCalls][1] == 3,
    'unpaired retry uses only old-binding cleanup and never polls or arms the new epoch')
c.refuseCleanup = false; c.now = 200; c.D.tick(200)
check(not c.D.nativePending and not c.D.boundMission and not c.D.boundEpoch and c.disarmed == 1,
    'applied cleanup receipt with ready=false retires only the confirmed old binding')
c.now = 300; c.D.tick(300)
check(c.cleanupAttempts == 3 and c.disarmed == 1, 'retired unpaired cleanup is not repeated')
c = fixture(true); down(c); c.D.tick(0); c.failReset = true; c.throwCleanup = true
c.W.unpairedHooks[1](); c.W.startHooks[1]()
check(c.D.maskOwned and c.D.nativePending and c.D.boundMission == 30010 and c.D.boundEpoch == 7 and not c.D.active,
    'new world reset preserves both failed named-mask and native cleanup ownership')
c.throwCleanup, c.failReset, c.W.paired = false, false, false; c.now = 100; c.D.tick(100)
check(not c.D.maskOwned and not c.D.nativePending and c.disarmed == 1,
    'both independent cleanup obligations complete on a later natural tick')
c = fixture(true); down(c); c.D.tick(0); c.cleanupAbsent = true; c.W.paired = false
c.W.unpairedHooks[1]()
check(not c.D.nativePending and not c.D.active and not c.disarmed and c.cleanupAttempts == 1,
    'already absent native lease acknowledgment retires Lua cleanup without native restoration')
c = fixture(true); down(c); c.D.tick(0); c.reply = {ready = true, applied = false}
c.W.unpairedHooks[1]()
check(c.D.nativePending and c.D.boundEpoch == 7, 'ready without applied never proves native cleanup')
c.reply = 'malformed'; c.W.paired = false; c.D.tick(100)
check(c.D.nativePending and c.D.boundEpoch == 7, 'malformed cleanup reply retains ownership for retry')
c.reply = nil; c.D.tick(200)
check(not c.D.nativePending and c.disarmed == 1, 'refused cleanup retries without rebinding its owner')
c = fixture(true); down(c); c.D.tick(0); c.W.startHooks[1]()
check(c.D.nativePending and c.D.maskOwned and not c.D.active,
    'start reset cannot silently forget an active old downed owner or its mask')
c.W.paired = false; c.D.tick(100)
check(not c.D.nativePending and not c.D.maskOwned and c.disarmed == 1,
    'start-preserved ownership is retired before any new paired poll')
c = fixture(true); down(c); c.D.tick(0); c.refuseCleanup = true; c.W.unpairedHooks[1]()
local count = #c.nativeCalls
check(not c.D.handleDeath(0) and #c.nativeCalls == count and not c.D.hitEnabled(),
    'pending old cleanup blocks death-handler arm and authenticated hit admission before the next tick')
c = fixture(true); down(c); c.D.tick(0); c.failReset = true; c.W.unpairedHooks[1]()
count = #c.nativeCalls
check(not c.D.handleDeath(0) and #c.nativeCalls == count and not c.D.hitEnabled(),
    'failed old mask release also blocks death-handler arm after native disarm succeeded')
-- A down cycle has an immutable cause and cannot reuse a spent generation.
c = fixture(true)
check(c.D.acceptStatus(status(1, 1, true, 1), 2, 0), 'begin immutable generation')
check(not c.D.acceptStatus(status(2, 1, true, 2), 2, 100), 'same generation cannot change incapacitation cause')
c.D.granted[2] = 1
check(c.D.acceptStatus(status(2, 1, false), 2, 100), 'matching completion observed')
check(not c.D.acceptStatus(status(3, 1, true, 1), 2, 200), 'spent downed generation cannot be reused')
check(c.D.acceptStatus(status(3, 2, true, 1), 2, 200), 'next independent downed cycle gets new generation')

-- Bounded host snapshots and grants reject malformed fields without touching
-- the native helper, elapsed hold or observed ordering history.
for _, values in ipairs({{0,0,0,0,0,0,0,0,0}, {1,0,1,0,0,0,0,0,0},
    {1,0,0,0,0,0,0,3001,2}, {1,0,0,0,0,0,0,1,0},
    {1,0,0,0,0,0,0,0,3}, {1,0,0,0,0,0,0,0,0,1}}) do
    c = fixture(false)
    c.W.handlers[OP.DOWNED_COMMIT](values)
    check(not c.D.remoteCommit and c.D.commitSeen == 0 and #c.nativeCalls == 0,
        'malformed host commit rejected without effects')
end
for _, values in ipairs({{2,0,1}, {2,1,0}, {2,1,-1}, {2,1,4294967296},
    {3,1,1}, {2,1,1,1}, {2,0/0,1}}) do
    c = fixture(false); down(c); c.D.tick(0); local calls = #c.nativeCalls
    c.W.handlers[OP.REVIVE_GRANT](values)
    check(not c.revived and c.D.receivedGrant == 0 and #c.nativeCalls == calls,
        'malformed grant rejected without native mutation')
end

-- Action is paired with the target's exact downed generation. A late held
-- packet from before a new cycle cannot continue the previous hold.
a, b = pair(); down(b); a.native.held = true
for _ = 1, 18 do step(a, b, 100) end
check(a.D.progress > 0, 'progress before changed target generation')
b.native.generation = b.native.generation + 1
step(a, b, 100); step(a, b, 100)
check(a.D.progress == 0 and not b.revived, 'old target-generation hold is invalidated')
for _ = 1, 15 do step(a, b, 100) end
check(not b.revived, 'changed generation starts a whole new hold')
if arg[2] then
    -- Events here emulate the native queue ONLY, to test transport and owner
    -- receipts. They are fixture data, not artificial retail-game damage.
    a, b = pair(); a.native.hitSequence, a.native.hitDamageMilli, a.native.hitCause = 1, 100000, 0
    step(a, b, 100); step(a, b, 100)
    check(b.appliedHits == 1 and b.native.life == 900 and not b.native.down and a.hitAcks == 1,
        'one natural-queue friendly bullet is applied once to real owner health')
    a.native.hitSequence, a.native.hitDamageMilli, a.native.hitCause = 1, 100000, 0
    step(a, b, 100); step(a, b, 100)
    check(b.appliedHits == 1 and b.native.life == 900, 'duplicate native shot serial never doubles friendly damage')
    for _, cause in ipairs({0,2}) do
        a, b = pair(); a.native.hitSequence, a.native.hitDamageMilli, a.native.hitCause = 7, cause == 2 and 0 or 1000000, cause
        step(a, b, 100); step(a, b, 100); step(a, b, 100)
        check(b.native.down and b.native.cause == (cause == 2 and 2 or 1) and b.D.records[2].down,
            'friendly lethal bullet and sleep dart enter actual owner protected cycle')
    end
    a, b = pair(); b.native.hitSequence, b.native.hitDamageMilli, b.native.hitCause = 3, 150000, 0
    step(a, b, 100); step(a, b, 100)
    check(a.appliedHits == 1 and a.native.life == 850, 'guest natural shot reaches host owner with identical routing')
    a, b = pair(); b.failFriendly = true
    a.native.hitSequence, a.native.hitDamageMilli, a.native.hitCause = 1, 100000, 0
    step(a, b, 100); step(a, b, 100)
    a.native.hitSequence, a.native.hitDamageMilli, a.native.hitCause = 2, 200000, 0
    step(a, b, 100); b.failFriendly = false; step(a, b, 100)
    check(b.appliedHits == 2 and b.native.life == 700, 'queued owner refusals preserve FIFO and each legitimate damage event')
    a, b = pair(); b.failFriendly = true
    a.native.hitSequence, a.native.hitDamageMilli, a.native.hitCause = 4, 100000, 0
    step(a, b, 100)
    for _ = 1, 7 do step(a, b, 100) end
    b.failFriendly = false; step(a, b, 100)
    check(not b.appliedHits, 'expired refused event cannot apply late damage')
    a, b = pair(); b.native.generation = 2; step(a, b, 100)
    b.W.handlers[OP.FRIENDLY_HIT]({9,0,1,100000,0},b.now); step(a,b,100)
    check(not b.appliedHits, 'old target downed generation cannot damage a later live cycle')
    a, b = pair()
    b.W.handlers[OP.FRIENDLY_HIT]({1,0,60000,100000,0},b.now); step(a,b,100)
    check(not b.appliedHits, 'unpublished target status is not a fresh owner capability')
    for _, v in ipairs({{0,0,1,1,0},{1,0,0,1,0},{1,0,1,1000000001,0},{1,0,1,1,1},{1,0,1,1,2,9}}) do
        a,b=pair(); b.W.handlers[OP.FRIENDLY_HIT](v,b.now); step(a,b,100)
        check(not b.appliedHits, 'malformed friendly hit does not invoke owner health receipt')
    end
    a,b=pair(); b.W.paired=false
    b.W.handlers[OP.FRIENDLY_HIT]({1,0,1,100000,2},b.now)
    check(not b.appliedHits, 'unpaired friendly packet cannot change native state')
end
-- Actual composed sleep authority: host owns one thirty-second deadline per
-- accepted generation, while the ordinary E grant can still wake early.
do
    local function beforeDeadline(host, guest, role)
        step(host, guest, 100); step(host, guest, 100)
        local timer = host.D.sleepTimers[role]
        check(timer and timer.generation == (role == 1 and host or guest).native.generation,
            'fresh accepted sleep gets only its exact host-owned generation deadline')
        while host.now < timer.at + 29900 do step(host, guest, 100) end
        return timer.at
    end
    for _, targetHost in ipairs({true, false}) do
        local host, guest = pair()
        local target, role = targetHost and host or guest, targetHost and 1 or 2
        target.native.life, target.native.sleepLife = 731, 731
        down(target, 2)
        local at = beforeDeadline(host, guest, role)
        check(not target.revived and target.native.down and target.native.life == 731,
            'sleep does not inflict lethal injury or wake before its fresh thirty-second deadline')
        check(target.D.hud(target.now).cause == 2 and (targetHost and guest or host).D.hud(host.now).teammateCause == 2,
            'both local HUD views distinguish asleep from wounded in either direction')
        for _ = 1, 4 do step(host, guest, 100) end
        check(host.now >= at + 30000 and target.revived == 1 and not target.native.down and target.native.life == 731,
            'host auto-wake uses exact native grant once and retains pre-dart health without E')
        for _ = 1, 5 do step(host, guest, 100) end
        check(target.revived == 1 and not next(host.D.sleepTimers),
            'healthy acknowledgements retire deadline and duplicate status cannot heal twice')
    end
    local host, guest = pair(); down(host, 2); down(guest, 2)
    beforeDeadline(host, guest, 1)
    check(not host.sharedFailure and not host.revived and not guest.revived,
        'two sleepers never enter shared lethal failure')
    for _ = 1, 6 do step(host, guest, 100) end
    check(host.revived == 1 and guest.revived == 1 and not host.native.down and not guest.native.down and not host.sharedFailure,
        'two sleepers each wake through their own host grant and generation')
    for _, woundedHost in ipairs({true, false}) do
        host, guest = pair()
        local wounded, sleeping, sleepRole = woundedHost and host or guest, woundedHost and guest or host, woundedHost and 2 or 1
        down(wounded, 1); down(sleeping, 2)
        beforeDeadline(host, guest, sleepRole)
        check(not host.sharedFailure and not wounded.revived,
            'mixed wounded and asleep waits for sleeper without releasing lethal lock or shared death')
        for _ = 1, 6 do step(host, guest, 100) end
        check(sleeping.revived == 1 and not sleeping.native.down and wounded.native.down,
            'only sleeper auto-wakes in mixed incapacity')
        sleeping.native.held = true
        for _ = 1, 34 do step(host, guest, 100) end
        check(wounded.revived == 1 and not wounded.native.down and not host.sharedFailure,
            'awake teammate can revive wounded survivor with the existing uninterrupted E grant')
    end
    for _, reset in ipairs({'stale', 'pause', 'reverse', 'new-generation'}) do
        host, guest = pair(); down(guest, 2)
        step(host, guest, 100); step(host, guest, 100)
        local first = host.D.sleepTimers[2].at
        for _ = 1, 280 do step(host, guest, 100) end
        check(not guest.revived, 'partial sleep duration before ' .. reset)
        if reset == 'stale' then
            host.now = host.now + 600; host.D.tick(host.now); host.tx = {}; guest.now = host.now
        elseif reset == 'pause' then
            host.inGame = false; step(host, guest, 100); host.inGame = true
        elseif reset == 'reverse' then
            host.now, guest.now = host.now - 200, guest.now - 200; step(host, guest, 0)
        else
            guest.native.generation = guest.native.generation + 1
            step(host, guest, 100); step(host, guest, 100)
        end
        step(host, guest, 100); step(host, guest, 100)
        local restarted = host.D.sleepTimers[2]
        check(restarted and restarted.at > first and not guest.revived,
            'stale context and changed generation cannot inherit old sleep elapsed time: ' .. reset)
        for _ = 1, 30 do step(host, guest, 100) end
        check(not guest.revived and guest.native.down,
            'old elapsed interval cannot auto-wake after timer restart: ' .. reset)
    end
    host, guest = pair(); down(guest, 2)
    local firstAt = beforeDeadline(host, guest, 2)
    guest.maskDelay = 300; host.failTx = OP.REVIVE_GRANT
    for _ = 1, 7 do step(host, guest, 100) end
    check(not guest.revived and host.D.pendingGrant and not host.D.granted[2],
        'failed wake grant transport retains protection and cannot self-authorize a healthy status')
    host.failTx = nil
    for _ = 1, 9 do step(host, guest, 100) end
    check(guest.revived == 1 and not guest.mask and not host.D.pendingGrant,
        'delayed reliable wake retries one exact grant until queued mask release acknowledges')
    local oldGeneration = guest.native.generation
    down(guest, 2); step(host, guest, 100); step(host, guest, 100)
    guest.W.handlers[OP.REVIVE_GRANT]({2, oldGeneration, 999})
    check(guest.native.down and guest.native.generation ~= oldGeneration and guest.revived == 1,
        'late old sleep wake cannot awaken a newer incapacitation generation')
    local old = copy(host.D.records[2])
    check(not host.D.acceptStatus(status(old.sequence, old.generation, true, 2), 2, host.now + 1000) and
        host.D.records[2].at == old.at, 'duplicate sleeper status cannot renew liveness or timer authority')
    host, guest = pair(); down(guest, 2); host.native.held = true
    for _ = 1, 35 do step(host, guest, 100) end
    check(guest.revived == 1 and host.now < 30000 and not guest.native.down,
        'normal three-second E interaction wakes sleep early without waiting for timer')
    host, guest = pair(); down(guest, 2); step(host, guest, 100); step(host, guest, 100)
    check(host.D.sleepTimers[2] ~= nil, 'sleep deadline exists before disconnect')
    host.W.unpairedHooks[1](); host.W.paired = false
    check(not next(host.D.sleepTimers) and not host.D.pendingGrant,
        'disconnect clears host sleep deadlines and pending wake grants')
end
-- A temporarily unreadable native owner is not continuous sleep authority.
-- Retain completed grants, but restart an uncommitted duration after re-read.
for _, refusal in ipairs({'healthy_poll', 'sleeping_poll', 'sleeping_lock'}) do
    local host, guest = pair()
    local target = refusal == 'healthy_poll' and guest or host
    down(target, 2); step(host, guest, 100); step(host, guest, 100)
    local role = target.W.role
    local originalAt = host.D.sleepTimers[role].at
    while host.now - originalAt < 29000 do step(host, guest, 100) end
    check(not target.revived, 'sleep still protected immediately before temporary ' .. refusal)
    if refusal == 'sleeping_lock' then host.failLock = true else host.refuseControl = true end
    step(host, guest, 100)
    check(not next(host.D.sleepTimers) and not target.revived and not host.D.pendingGrant,
        'every native poll/lock refusal retires uncommitted sleep elapsed time: ' .. refusal)
    host.failLock, host.refuseControl = false, false
    step(host, guest, 100); step(host, guest, 100)
    check(host.D.sleepTimers[role] and host.D.sleepTimers[role].at > originalAt,
        'fresh native recovery begins a new sleep duration for the same protected generation: ' .. refusal)
    for _ = 1, 30 do step(host, guest, 100) end
    check(not target.revived and target.native.down,
        'pre-refusal duration cannot trigger an early wake after recovery: ' .. refusal)
end
-- Native pose retirement precedes releasing the named mask and health cycle.
do
    local host, guest = pair()
    guest.native.nativeSleepActive = true
    down(guest, 2); step(host, guest, 100)
    host.native.held = true
    for _ = 1, 40 do step(host, guest, 100) end
    check(guest.wakeAttempts and guest.wakeAttempts > 0 and guest.mask and not guest.maskResets and not guest.revived,
        'host grant waits for actual native sleep retirement while all controls stay masked')
    guest.nativeRetired = true
    for _ = 1, 5 do step(host, guest, 100) end
    check(guest.revived == 1 and not guest.mask and guest.maskResets == 1,
        'only the exact ready native retirement receipt permits final mask release and revive')
    local c = fixture(true); c.native.nativeSleepActive = true; down(c, 2); c.D.tick(0)
    c.refuseCleanup = true; c.W.paired = false; c.W.unpairedHooks[1]()
    check(c.mask and c.D.maskOwned and c.D.nativeSleepActive and c.D.nativePending and not c.maskResets,
        'unpair cannot discard masks or native pose lease while retirement is pending')
    c.W.startHooks[1](); c.D.tick(100)
    check(c.mask and c.D.nativeSleepActive and c.D.nativePending,
        'new scene cannot erase old native sleep cleanup ownership')
    c.refuseCleanup = false; c.D.tick(200)
    check(not c.mask and not c.D.maskOwned and not c.D.nativePending and not c.D.nativeSleepActive,
        'confirmed old native sleep cleanup releases only its own masks once')
end
do
    local c = fixture(true); c.D.tick(0)
    check(c.D.partnerSleepBridge == false and not c.bridgePackets, 'normal capability false adds no per-tick bridge traffic')
    c = fixture(true); c.bridgeCap = true; c.now = 100
    check(c.D.acceptStatus(status(1, 5, true, 2), 2, 100), 'native bridge fixture first accepts genuine peer sleep status')
    c.D.tick(100); local v = c.bridgePackets[#c.bridgePackets]
    check(v[1] == 30010 and v[2] == 7 and v[3] == 1 and v[4] == 1 and v[5] == 5 and v[6] == 1 and v[7] == 2 and v[8] == 1 and v[9] == 0,
        'only accepted peer generation/cause/sequence and receive age reach native bridge')
    check(not c.D.acceptStatus(status(2, 4, true, 2), 2, 200), 'older peer generation is rejected before bridge')
    c.now = 200; c.D.tick(200); v = c.bridgePackets[#c.bridgePackets]
    check(v[4] == 1 and v[5] == 5 and v[9] == 100, 'duplicate presentation poll preserves original peer receive age')
    c.now = 601; c.D.tick(601); v = c.bridgePackets[#c.bridgePackets]
    check(v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7] + v[8] + v[9] == 0, 'stale peer emits explicit native presentation clear')
    c.now = 700; c.D.acceptStatus(status(3, 5, true, 2), 2, 700); c.refuseControl = true; c.D.tick(700)
    check(c.bridgePackets[#c.bridgePackets][1] == 0, 'native local poll failure withdraws partner presentation admission')
    c.refuseControl = nil; c.inGame = false; c.D.tick(800)
    check(c.bridgePackets[#c.bridgePackets][1] == 0, 'not-in-game state cannot retain native partner target')
    c.inGame = true; c.now = 900; c.D.acceptStatus(status(4, 5, true, 2), 2, 900); c.D.tick(900)
    c.W.paired = false; c.W.unpairedHooks[1]()
    check(c.bridgePackets[#c.bridgePackets][1] == 0 and c.D.partnerSleepBridge == nil, 'unpair/reset withdraws bridge without faking healthy peer')
end
print(checks .. ' downed policy checks: native gating, 3D range, continuous holds, generations, ordered status, grants, shared failure and cleanup; no game calls')
