-- M1 host-owned companion (docs/vehicles-companions-design.md 6.1): lua/coop/world_companion.lua
-- with world_follow_seed.lua and world_follow.lua on recording game APIs. No game process.
-- Usage: lua51 world_companion_lua_check.lua <world_companion.lua> <world_follow_seed.lua> <world_follow.lua>
-- Written before the module: every check failed (no buddy seed keys, no W.companion).
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(tostring(checks) .. ': ' .. why, 2) end
end
local function slurp(path)
    local f = assert(io.open(path, 'rb'))
    local s = f:read('*a')
    f:close()
    return s
end
check(#slurp(assert(arg[1])):gsub('\r\n', '\n') <= 32000, 'the chunk is within the 32000-byte world chunk limit')
local OP = {TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13, REJECT = 4}
local NONE, HORSE, DOG, QUIET, WALKER = 0, 1, 2, 3, 4
local function Fixture(role, o)
    o = o or {}
    local c = {calls = {}, tx = {}, logs = {}, notes = {}, saves = {}, masks = {}, time = 1000, inGame = true, active = o.active or NONE,
        block = o.block ~= false}
    local env = setmetatable({}, {__index = _G})
    env.vars = {missionCode = 30010, locationCode = 10, mbLayoutCode = 0, mbClusterId = 0, playerPosX = 1, playerPosY = 2,
        playerPosZ = 3, playerRotY = 0, buddyType = o.buddyType or HORSE, initialBuddyStatus = 0,
        initialBuddyPos = {[0] = 0, [1] = 0, [2] = 0}, initialBuddyRotY = 0, buddyGearAmmoCount = {[0] = 0, [1] = 0},
        buddyGearStockAmmoCount = {[0] = 0, [1] = 0}, buddyDogEyeType = o.eye or 1}
    for _, n in ipairs({"buddyQuietEquipType", "buddyQuietCostumeType", "buddyDogFlag", "buddyDogMode", "buddyDogEquipType",
        "buddyDogColorType", "buddyGearBodyGrade", "buddyGearEquipGrade", "buddyGearReloadGrade", "buddyGearHeadType",
        "buddyGearHeadGrade", "buddyGearArmType", "buddyGearArmGrade", "buddyGearMainWPType", "buddyGearMainWPGrade",
        "buddyGearSubWPType", "buddyGearSubWPGrade", "buddyGearColoring0", "buddyGearColoring1", "buddyHorseType",
        "buddyHorseColorType", "buddyHorseHeadDesignType", "buddyHorseFootDesignType"}) do env.vars[n] = 0 end
    env.mvars, env.gvars, env.svars = {}, {}, {}
    env.BuddyType = {NONE = NONE, HORSE = HORSE, DOG = DOG, QUIET = QUIET, WALKER_GEAR = WALKER, BATTLE_GEAR = 5}
    env.BuddyInitStatus = {NORMAL = 0}
    env.Vector3 = function(x, y, z) return {x = x, y = y, z = z} end
    env.DemoDaemon = {IsDemoPlaying = function() return false end}
    env.PlayerPad = {CALL = 0x200}
    env.PlayerStatus = {CQC_CONTINUOUS = 77}
    env.PlayerInfo = {OrCheckStatus = function(t) return c.cqc == true and t[1] == 77 end}
    env.Player = {SetPadMask = function(t) c.masks[#c.masks + 1] = {'set', t} end,
        ResetPadMask = function(t) c.masks[#c.masks + 1] = {'reset', t} end}
    local function rec(name, ...) c.calls[#c.calls + 1] = {name, ...} end
    env.TppBuddy2BlockController = {
        CallBuddy = function(t, p, yaw) rec('CallBuddy', t, p, yaw); c.active = t; return true end,
        CallBuddyWithHalo = function(t) rec('CallBuddyWithHalo', t); return true end,
        ReserveCallBuddy = function(t, s, p, yaw) rec('ReserveCallBuddy', t, s, p, yaw) end,
        DeleteBuddy = function() rec('DeleteBuddy'); c.active = NONE end,
        Load = function() rec('Load'); c.block = true end,
        GetActiveBuddyType = function() return c.active end,
        IsBlockActive = function() return c.block end}
    env.TppBuddyService = {IsDisableBuddyType = function() return false end,
        SetDisableBuddyType = function(t) rec('SetDisableBuddyType', t) end,
        UnsetDisableBuddyType = function(t) rec('UnsetDisableBuddyType', t) end}
    env.TppSave = {SaveVarsToSlot = function(slot) c.saves[#c.saves + 1] = {slot = slot, buddyType = env.vars.buddyType, eye = env.vars.buddyDogEyeType} end}
    env.TppStory = {CanArrivalQuietInMB = function() return true end}
    env.TppDefine = {MISSION_LOAD_TYPE = {MISSION_FINALIZE = 1, MISSION_ABORT = 2, MISSION_RESTART = 3, CONTINUE_FROM_CHECK_POINT = 4}}
    env.TppMission = {Load = function(m) rec('load', m, env.vars.buddyType) end, UpdateCheckPoint = function() rec('checkpoint'); return true end}
    env.e = {}
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {}, bindHooks = {},
        level = 3, role = role, host = role == 1, active = true, mission = 30010, epoch = 7, paired = true, eff = 3, mode = 1,
        companionOn = o.on ~= false}
    env.e.MgsCoopWorld = W
    W.get = function(t, k) if t == nil then return nil end return t[k] end
    W.cfg = function() return {3, role, 1, 1, 7, 0, 1, 55} end
    W.h = function() return 1 end
    W.now = function() return c.time end
    W.tick32 = function(n) return n end
    W.inGame = function() return c.inGame end
    W.nextRid = function() return 1 end
    W.log = function(s) c.logs[#c.logs + 1] = s end
    W.receipt = function(s) if s:find('MGS_COOP_CHARACTER_V1', 1, true) then return o.character or 'quiet', 'dd_male', false end end
    W.tx = function(op, ...) c.tx[#c.tx + 1] = {op = op, v = {...}}; return true end
    W.fail = function(s) c.failure = s end
    W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
    W.peer = function() return nil end
    W.n = function(v) return v end
    for _, path in ipairs({arg[2], arg[3], arg[1]}) do
        local chunk = assert(loadfile(path)); setfenv(chunk, env); chunk()
    end
    W.follow.install({3, role, 1, 1, 7, 0, 1, 55, 0, 0, 0, 0, 0, 0, 0, o.on ~= false and 1 or 0})
    W.api = {}
    for _, f in ipairs(W.bindHooks) do f(W.api) end
    for _, f in ipairs(W.startHooks) do f() end
    c.W, c.env, c.C, c.F, c.S = W, env, W.companion, W.follow, W.followSeeds
    function c.find(s) for i = #c.logs, 1, -1 do if c.logs[i]:find(s, 1, true) then return c.logs[i] end end end
    function c.called(name) local n = 0; for _, v in ipairs(c.calls) do if v[1] == name then n = n + 1 end end; return n end
    function c.tick(ms) c.time = c.time + (ms or 200); W.companion.tick(c.time) end
    return c
end
-- Seeds as the host would send them: the buddy keys from a host copy with D-Dog and eye type 3.
local function hostSeeds(buddy)
    local h = Fixture(1, {buddyType = buddy or DOG, eye = 3})
    local values = {}
    for k, key in ipairs(h.S.keys) do
        if key[6] == 'buddy' then
            local n = key[2] or 1
            local data = h.env.vars[key[1]]
            for i = 0, n - 1 do values[#values + 1] = {k, i, h.S.encode(key, key[2] and data[i] or data)} end
        end
    end
    return values
end
local function follow(c, values)
    c.F.seeds[5] = {mission = 30010, values = values, ready = true, digest = 0}
    c.F.current = {5, 6, 40010, 30010, 0, 0, 0, 0, 0, 0, 1}
    c.env.TppMission.Load(30010)
end

-- The seed schema: 30 buddy keys, always sent, inside the 1..127 key index.
local c = Fixture(2)
local n = 0
for _, key in ipairs(c.S.keys) do if key[6] == 'buddy' then n = n + 1; check(key[4] == true, 'buddy keys are vars') end end
check(n == 30 and #c.S.keys <= 127, 'thirty buddy keys; the key count fits the 1..127 seed index')
local values = hostSeeds(DOG)
check(#values == 34, 'the buddy values (three positions and two pairs of gear ammo)')

-- Guest, companion off: the buddy keys are not applied; the guest keeps its horse.
c = Fixture(2, {on = false})
follow(c, values)
check(c.env.vars.buddyType == HORSE and not c.C.own, 'off: the guest loads its own buddy (no seed applied)')

-- Guest, companion on: own kept, the host's dog and look in vars before Load.
c = Fixture(2)
follow(c, values)
check(c.calls[#c.calls][1] == 'load' and c.calls[#c.calls][3] == DOG, 'the retail Load sees the host dog')
check(c.env.vars.buddyDogEyeType == 3, 'the host dog look is seeded')
check(c.C.own and c.C.own.buddyType == HORSE and c.C.own.buddyDogEyeType == 1, 'the guest keeps its own buddy vars')
check(c.find('event=companion;role=guest;step=own_kept;why=seed;own=horse;'), 'own kept is logged')
check(c.find('step=seed_apply;kind=dog;own=horse;mission=30010;'), 'seed apply is logged')
-- D8 (b): a save writes the guest's own vars; vars stay the host's afterwards.
c.env.TppSave.SaveVarsToSlot(3)
check(c.saves[1].buddyType == HORSE and c.saves[1].eye == 1, 'the save slot gets the guest own buddy')
check(c.env.vars.buddyType == DOG and c.env.vars.buddyDogEyeType == 3, 'vars are the host values again after the save')
check(c.find('step=save_own;slot=3;own=horse;live=dog;n=1;'), 'the save policy is logged')
-- The load is confirmed once the block has the host's dog.
c.W.mission = 30010
for _, f in ipairs(c.W.startHooks) do f() end
c.tick()
check(not c.find('step=loaded;'), 'not loaded while the block has none')
c.active = DOG
c.tick()
check(c.find('event=companion;role=guest;step=loaded;kind=dog;source=seed;mission=30010;'), 'loaded kind=dog source=seed')
-- The guest sends its character: Quiet (coop.ini quiet).
local sent
for _, t in ipairs(c.tx) do if t.op == 56 then sent = t end end
check(sent and sent.v[2] == 6 and sent.v[3] == 1, 'CHARACTER: quiet, flag 1')
-- The CALL mask (call menu, whistle), released in a CQC hold.
check(c.masks[1] and c.masks[1][1] == 'set' and c.masks[1][2].buttons == 0x200 and c.masks[1][2].settingName == 'MgsCoopCompanion',
    'the guest masks CALL')
check(c.find('step=guard;path=call_menu;via=pad_mask;') and c.notes[1] and c.notes[1][1] == 13 and c.notes[1][2] == 1,
    'the mask is logged with toast 13/1')
c.cqc = true; c.tick(300)
check(c.masks[#c.masks][1] == 'reset', 'a CQC hold releases the mask (CALL interrogates)')
c.cqc = false; c.tick(300)
check(c.masks[#c.masks][1] == 'set', 'the mask returns after the hold')
-- Lua entry points: another type is refused with a toast; the host kind passes.
c.W.handlers[48]({1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0}, c.time)
local before = c.called('CallBuddy')
check(c.env.TppBuddy2BlockController.CallBuddy(HORSE, nil, 0) == false and c.called('CallBuddy') == before,
    'a guest call of another type is refused')
check(c.find('step=refused;path=lua_CallBuddy;type=horse;host=dog;') and c.notes[#c.notes][2] == 2, 'refusal logged with toast 13/2')
check(c.env.TppBuddy2BlockController.CallBuddy(DOG, nil, 0) == true, 'the host kind passes')
check(c.env.TppBuddy2BlockController.DeleteBuddy() == false, 'a guest DeleteBuddy is refused while the host has a dog')
check(c.find('step=hook;fn=CallBuddy;type=horse;'), 'the E4 census logs the hook')
check(c.W.api.buddyCall == c.C.orig.CallBuddy, 'world_buddy/debug_buddy keep the original (their own guards)')
-- Reconcile: the host changes to D-Horse; after 3 s the guest calls a horse (original, no refusal).
c.W.handlers[48]({2, 0, 2, 0, 1, 10000, 0, 20000, 0, 0, 0, 0}, c.time)
c.tick(500)
check(not c.find('step=reconcile;'), 'no reconcile before 3 s of mismatch')
c.time = c.time + 3500
c.W.handlers[48]({3, 0, 2, 0, 1, 10000, 0, 20000, 0, 0, 0, 0}, c.time)
c.tick(200)
check(c.active == HORSE and c.find('step=reconcile;via=call;kind=horse;have=dog;ok=1;'), 'reconcile by CallBuddy to the host kind')
-- A solo Load gives the guest its own buddy back.
c.F.current = nil
c.env.TppMission.Load(30020)
check(not c.C.own and c.env.vars.buddyType == HORSE and c.find('step=own_restored;why=solo;own=horse;'), 'solo travel restores own')

-- Host: the guest plays Quiet; the host's Quiet is locked before its seeds.
c = Fixture(1, {buddyType = QUIET})
c.W.handlers[56]({1, 6, 1})
check(c.C.guestQuiet == true and c.find('step=character;guest_code=6;guest_char=quiet;'), 'the host learns the guest plays Quiet')
c.tick()
check(c.called('SetDisableBuddyType') == 1 and c.find('step=quiet_lock;via=disable_type;guest_char=quiet;'), 'disable byte set')
check(c.notes[1] and c.notes[1][1] == 13 and c.notes[1][2] == 3, 'the host gets toast 13/3')
c.F.outgoing = nil
c.env.TppMission.Load(30010)
check(c.calls[#c.calls][1] == 'load' and c.calls[#c.calls][3] == NONE, 'the sortie loads no buddy')
check(c.find('step=quiet_lock;via=sortie_rewrite;guest_char=quiet;') and c.find('step=seed;kind=none;mission=30010;'),
    'sortie rewrite and the host seed line')
c.env.TppSave.SaveVarsToSlot(1)
check(c.saves[1].buddyType == QUIET, 'the host save keeps its Quiet choice')
-- Mid-mission: a Quiet already out is sent away.
c.active = QUIET
c.tick(6000)
check(c.called('DeleteBuddy') == 1 and c.find('step=quiet_lock;via=delete_buddy;'), 'a Quiet already out is deleted')
-- The Mother Base cell Quiet is hidden on the host copy (and not for the asset query with true).
c.env.vars.missionCode = 30050
check(c.env.TppStory.CanArrivalQuietInMB() == false and c.env.TppStory.CanArrivalQuietInMB(true) == true, 'cell Quiet hidden')
check(c.find('event=companion;role=host;step=mbquiet_hidden;'), 'mbquiet_hidden logged')
-- The host sends its kind.
local kind
for _, t in ipairs(c.tx) do if t.op == 48 then kind = t.v[3] end end
check(kind == 0, 'COMPANION carries kind none after the lock')
-- Host with a guest who is not Quiet: no lock.
c = Fixture(1, {buddyType = QUIET})
c.W.handlers[56]({1, 2, 0})
c.tick()
c.env.TppMission.Load(30010)
check(c.calls[#c.calls][3] == QUIET and c.called('SetDisableBuddyType') == 0, 'no lock when the guest is not Quiet')
-- Guest cell hide on its own copy (it plays Quiet); off when the key is off.
c = Fixture(2)
c.env.vars.missionCode = 30050
check(c.env.TppStory.CanArrivalQuietInMB() == false, 'the guest copy hides the cell Quiet too')
c = Fixture(2, {on = false})
c.env.vars.missionCode = 30050
check(c.env.TppStory.CanArrivalQuietInMB() == true, 'off: the retail answer')
-- Seed lever (lab): vars.buddyType and a checkpoint; the own copy follows when kept.
c = Fixture(2)
follow(c, values)
local ok, msg = c.C.seedLever('walker')
check(ok and msg:find('seed_buddy;kind=walker;vars_type=4;checkpoint=1;', 1, true) and c.C.own.buddyType == WALKER, 'seed lever')
c.W.mode = 2
check(not c.C.seedLever('horse'), 'the seed lever is lab only')
print('world_companion_lua_check: ' .. checks .. ' checks passed')
