-- Real staged Lua policy with a queued named-mask fake; no game, native input or saves.
local checks = 0
local function check(value, why) checks = checks + 1; assert(value, checks .. ': ' .. why) end
local function copy(t)
    local result = {}
    for k, v in pairs(t) do result[k] = type(v) == 'table' and copy(v) or v end
    return result
end
local function fixture()
    local c = {now = 1000, sets = 0, resets = 0, native4 = 0, tx = {}, masks = {}, announces = {},
        n = {ready = true, down = false, held = false, generation = 0, cause = 0, pos = {0, 0, 0}}}
    local env = setmetatable({e = {}, vars = {missionCode = 30010}, Player = {},
        Tpp = {MgsCoop = {playerPackageSelected = true}}}, {__index = _G})
    local W = {OP = {DOWNED_STATUS = 36, DOWNED_COMMIT = 37, REVIVE_GRANT = 38},
        active = true, paired = true, eff = 3, mission = 30010, epoch = 7, role = 1, host = true,
        handlers = {}, startHooks = {}, stopHooks = {}, pairedHooks = {}, unpairedHooks = {}, tickHooks = {},
        recovery = {dead = false}}
    W.get = function(t, k) return t and t[k] end
    W.now = function() return c.now end
    W.log = function() end
    W.inGame = function() return not c.outOfGame end
    W.tx = function(op, ...) c.tx[#c.tx + 1] = {op, {...}}; return true end
    W.receipt = function(text)
        if text:match("^return 'MGS_COOP_DOWNED_HUD_V[123]'") then return true end
        local m, ep, cmd, gen = text:match("^return 'MGS_COOP_DOWNED_V1' %-%- (%d+) (%d+) (%d+) (%d+)$")
        check(m == '30010' and ep == '7', 'all control receipts retain exact mission and epoch')
        cmd, gen = tonumber(cmd), tonumber(gen)
        if cmd ~= 3 and (not W.active or not W.paired or c.nativeUnavailable) then return nil end
        local r = copy(c.n)
        if cmd == 4 then
            check(gen == 1912501439, 'command4 uses exact named mask Fox hash')
            c.native4 = c.native4 + 1
            if c.throwObserve then error('receipt refusal') end
            if c.tableUnstable then r.applied = false; return r end
            r.reviveMaskObserved, r.reviveMaskPresent = true, c.masks.MgsCoopRevive ~= nil
            r.applied = not r.down and c.masks.MgsCoopRevive == 0x20
            r.held = r.applied and c.useHeld == true and c.masks.Cutscene ~= 0x20
            if c.changedGeneration then r.generation = r.generation + 1 end
            return r
        elseif cmd == 1 then r.applied = c.masks.MgsCoopDowned ~= nil
        elseif cmd == 2 then r.applied = false
        elseif cmd == 3 then r.applied = true
        end
        return r
    end
    env.Player.IsInstanceValid = function() return not c.invalidPlayer end
    env.Player.SetPadMask = function(options)
        if options.settingName == 'MgsCoopDowned' then c.masks.MgsCoopDowned = options.buttons; return end
        check(options.settingName == 'MgsCoopRevive' and options.except == false and options.buttons == 0x20 and
            options.sticks == 0 and options.triggers == 0, 'revive mask changes only native use button')
        c.sets = c.sets + 1
        if c.throwSet then error('set failed') end
        if c.queueSet then c.pendingSet = true
        elseif not c.fullTable then c.masks.MgsCoopRevive = options.buttons end
    end
    env.TppUiCommand = {AnnounceLogView = function(text) c.announces[#c.announces + 1] = text end}
    env.Player.ResetPadMask = function(options)
        if options.settingName == 'MgsCoopDowned' then c.masks.MgsCoopDowned = nil; return end
        check(options.settingName == 'MgsCoopRevive' and options.buttons == nil, 'cleanup resets only its own name')
        c.resets = c.resets + 1
        if c.throwReset then error('reset failed') end
        if c.queueReset then c.pendingReset = true else c.masks.MgsCoopRevive = nil end
    end
    env.e.MgsCoopWorld = W
    local chunk = assert(loadfile(arg[1])); setfenv(chunk, env); chunk()
    local untouched = function() c.otherTicks = (c.otherTicks or 0) + 1 end
    W.tickHooks = {untouched, W.downed.tick}
    chunk = assert(loadfile(arg[2])); setfenv(chunk, env); chunk()
    check(W.tickHooks[1] == untouched and W.tickHooks[2] == W.downed.tick,
        'only exact captured original downed tick is replaced by companion wrapper')
    c.W, c.D, c.env = W, W.downed, env
    c.D.records[2] = {ready = true, down = true, generation = 1, cause = 1, pos = {3000, 0, 0}, at = c.now}
    function c.step(ms)
        c.now = c.now + (ms or 0)
        for _, f in ipairs(W.tickHooks) do f(c.now) end
        local l = c.D.records[1]
        return l and l.ready == true and l.held == true
    end
    function c.flush()
        if c.pendingSet then c.masks.MgsCoopRevive, c.pendingSet = 0x20, nil end
        if c.pendingReset then c.masks.MgsCoopRevive, c.pendingReset = nil, nil end
    end
    return c
end

local c = fixture(); c.useHeld = true
check(c.step() and c.sets == 1 and c.masks.MgsCoopRevive == 0x20, 'exact3m live teammate admits observed use hold')
check(c.D.hud(c.now).inRange and c.D.hud(c.now).near, 'revive hint appears only after native E mask is observed, and the 3m approach remains')
check(#c.announces == 1 and c.announces[1]:find('Hold Action (E)', 1, true) and c.announces[1]:find('Pickup is unavailable', 1, true),
    'game log says to hold Action and that pickup is unavailable')
check(c.D.progress == 0, 'first qualified hold begins at zero elapsed')
check(c.step(100) and c.D.progress == 100 and c.sets == 1 and #c.announces == 1, 'observed mask maintains hold without duplicate setters or repeated prompts')
c.useHeld = false; check(not c.step(100) and c.D.progress == 0, 'release immediately clears revive progress')
check(c.masks.MgsCoopRevive == 0x20, 'use pickup stays suppressed throughout nearby revive eligibility')
c.D.records[2].pos[1] = 3001
check(not c.step() and c.masks.MgsCoopRevive == nil and not c.D.reviveMaskOwned and not c.D.hud(c.now).near, 'leaving3m resets and confirms absence')
check(c.announces[#c.announces]:find('Get closer', 1, true) and c.announces[#c.announces]:find('Pickup is unavailable', 1, true) and not c.announces[#c.announces]:find('Hold', 1, true),
    'leaving 3m tells the player to get closer and that pickup is still unavailable')
check(c.masks.MgsCoopDowned == nil, 'revive interaction never creates downed mask')

for _, change in ipairs({
    function(f) f.D.records[2].ready = false end,
    function(f) f.D.records[2].down = false end,
    function(f) f.D.records[2].pos[2] = 1 end,
    function(f) f.D.records[2].pos = {0/0, 0, 0} end,
    function(f) f.D.records[2].at = f.now + 1 end,
    function(f) f.D.records[2].at = f.now - 500 end,
    function(f) f.W.paired = false end,
    function(f) f.W.active = false end,
    function(f) f.W.eff = 2 end,
    function(f) f.W.recovery.dead = true end,
    function(f) f.outOfGame = true end,
    function(f) f.env.Tpp.MgsCoop.playerPackageSelected = false end,
    function(f) f.invalidPlayer = true end
}) do
    c = fixture(); c.useHeld = true; change(c)
    check(not c.step() and c.sets == 0 and c.D.progress == 0, 'ineligible use remains normal and never advances revive')
end
c = fixture(); c.D.records[2].at = c.now - 499; c.useHeld = true
check(c.step(), '499ms record still qualifies')
c = fixture(); c.now = 1000.125; c.D.records[2].at = 900.375; c.useHeld = true
check(c.step(), 'native raw elapsed fractional milliseconds remain valid fresh timestamps')
c = fixture(); c.useHeld = true; check(c.step(), 'arm before expiry')
check(not c.step(500) and c.masks.MgsCoopRevive == nil, '500ms expiry releases native use mask')

c = fixture(); c.useHeld = true; c.fullTable = true
check(not c.step() and c.D.progress == 0, 'full mask table cannot fabricate applied hold')
check(not c.step(50) and c.sets == 1, 'failed observation does not spam setters')
c.fullTable = false; check(c.step(50) and c.sets == 2, 'available slot can retry and qualify')
c = fixture(); c.useHeld = true; c.queueSet = true
check(not c.step() and c.D.progress == 0, 'queued mask does not advance hold before observed')
check(not c.D.hud(c.now).inRange and c.D.hud(c.now).near and c.D.hud(c.now).teammateDown, 'queued mask preserves teammate guidance without advertising unsafe use hold')
check(#c.announces == 1 and c.announces[1]:find('not ready', 1, true) and not c.announces[1]:find('Hold', 1, true) and c.announces[1]:find('Pickup is unavailable', 1, true),
    'close enough names Action as not ready and pickup as unavailable')
c.flush(); check(not c.step(50), 'queued installation waits bounded retry')
c.queueSet = false; check(c.step(50), 'observed queued install qualifies after retry')
c = fixture(); c.useHeld = true; c.masks.Cutscene = 0x20
check(not c.step() and c.D.progress == 0 and c.masks.Cutscene == 0x20, 'all other masks retain their use restriction')
for _, field in ipairs({'tableUnstable', 'throwObserve', 'changedGeneration', 'throwSet'}) do
    c = fixture(); c.useHeld = true; c[field] = true
    check(not c.step() and c.D.progress == 0, 'refused/mismatched control4 cannot grant held input: '..field)
end

c = fixture(); c.useHeld = true; check(c.step(), 'arm before queued cleanup')
c.queueReset = true; c.D.records[2].down = false; c.step()
check(c.D.reviveMaskOwned == true and c.masks.MgsCoopRevive == 0x20, 'successful queued reset is not assumed delivered')
c.W.active, c.W.paired = false, false
c.now = c.now + 50; c.D.stop(); check(c.resets == 1, 'inactive repeated stop respects100ms cleanup retry')
c.now = c.now + 50; c.D.stop(); check(c.resets == 2 and c.D.reviveMaskOwned, 'inactive stop retries exact own name')
c.flush(); c.queueReset = false; c.W.active, c.W.paired = true, true
c.D.records[2] = {ready = true, down = false, generation = 1, cause = 0, pos = {0, 0, 0}, at = c.now}
c.step(); check(not c.D.reviveMaskOwned, 'later coherent absence acknowledges pending cleanup')

c = fixture(); c.useHeld = true; c.masks.Unrelated = 0x800
check(c.step(), 'captured tick dispatch arms before native poll refusal')
c.nativeUnavailable, c.queueReset = true, true
check(not c.step(100) and c.resets == 1 and c.W.active and c.D.reviveMaskOwned,
    'captured active tick releases own E mask when native0 poll refuses')
c.step(50); check(c.resets == 1, 'captured poll refusal cleanup remains bounded')
c.step(50); check(c.resets == 2 and c.masks.Unrelated == 0x800 and c.otherTicks == 4,
    'captured active tick retries cleanup without touching unrelated masks or hooks')

c = fixture(); c.useHeld = true; check(c.step(), 'arm before death')
c.throwReset = true; c.n.down, c.n.cause, c.n.generation = true, 1, 1
check(not c.step(100) and c.masks.MgsCoopDowned ~= nil and c.D.reviveMaskOwned, 'failed revive cleanup does not block normal downed rearm')
c.throwReset = false; c.D.records[2].at = c.now; c.step(100)
check(not c.D.reviveMaskOwned and c.masks.MgsCoopDowned ~= nil, 'downed cmd4 can confirm absence without clearing downed own mask')

for _, flow in ipairs({'unpaired', 'stop', 'paired', 'start'}) do
    c = fixture(); c.useHeld = true; check(c.step(), 'arm before lifecycle '..flow)
    if flow == 'unpaired' then c.W.paired = false; for _, f in ipairs(c.W.unpairedHooks) do f() end
    elseif flow == 'stop' then for _, f in ipairs(c.W.stopHooks) do f(true) end
    elseif flow == 'paired' then for _, f in ipairs(c.W.pairedHooks) do f(true) end
    else for _, f in ipairs(c.W.startHooks) do f() end end
    check(c.masks.MgsCoopRevive == nil, 'captured original hooks still invoke new cleanup: '..flow)
end
c = fixture(); c.useHeld = true; check(c.step(), 'arm before non-final world stop')
for _, f in ipairs(c.W.stopHooks) do f(false) end
check(c.masks.MgsCoopRevive == 0x20, 'non-final W1 stop preserves ongoing paired revive')
c.invalidPlayer = true; c.D.stop(); check(c.D.reviveMaskOwned, 'invalid Player guard never claims cleanup success')

-- Use is removed from the native button stream, while other button bits and world ownership are unchanged.
c = fixture(); c.useHeld = true; c.step()
local physical = 0x20 + 0x800 + 0x1000
local nativeButtons = physical - c.masks.MgsCoopRevive
check(nativeButtons == 0x1800 and c.D.records[1].held, 'pickup/CQC use is masked while independent movement/aim remain native')
c.D.records[2].down = false; c.step()
check(c.masks.MgsCoopRevive == nil and physical == 0x1820, 'ordinary use returns on exit without changing original input')

c = fixture(); c.n.held = true; c.D.records[2].pos[1] = 3001
check(not c.step() and c.sets == 0, 'ordinary native use held away from teammate never starts revive')

-- Execute the real staged world-sync OnUpdate wrapper while inactive; D.tick is not entered there.
do
    local env = setmetatable({e = {}}, {__index = _G})
    local updates, stops = 0, 0
    env.e.OnUpdate = function(v) updates = updates + 1; return v end
    local chunk = assert(loadfile(arg[3])); setfenv(chunk, env); chunk()
    local W = env.e.MgsCoopWorld
    W.downed = {reviveMaskOwned = true, stop = function() stops = stops + 1 end}
    check(env.e.OnUpdate('forwarded') == 'forwarded' and updates == 1 and stops == 1,
        'real inactive world update services revive cleanup while preserving original update return')
    W.downed.reviveMaskOwned = nil; env.e.OnUpdate('next')
    check(stops == 1, 'inactive update does not invoke cleanup without any owned mask')
    W.downed.maskOwned = true; env.e.OnUpdate('downed')
    check(stops == 2, 'original downed inactive cleanup gate is retained')
    W.downed.stop = function() error('cleanup temporarily refused') end
    check(env.e.OnUpdate('after-error') == 'after-error', 'inactive cleanup refusal preserves original update')
end
print('world_revive_input_lua_check: '..checks..' checks passed (policy fixture only)')
