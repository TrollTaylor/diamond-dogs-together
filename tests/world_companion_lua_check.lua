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
        "buddyHorseColorType", "buddyHorseHeadDesignType", "buddyHorseFootDesignType", "initialBuddyStatusMissionStart",
        "initialBuddyRotYMissionStart"}) do env.vars[n] = 0 end
    env.vars.buddyTypeMissionStart = o.buddyType or HORSE
    env.vars.initialBuddyPosMissionStart = {[0] = 0, [1] = 0, [2] = 0}
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
    env.TppSave = {SaveVarsToSlot = function(slot) c.saves[#c.saves + 1] = {slot = slot, buddyType = env.vars.buddyType,
        eye = env.vars.buddyDogEyeType, start = env.vars.buddyTypeMissionStart, dead = env.vars.deadBuddyType,
        flag = env.vars.buddyCommonFlag} end}
    env.TppStory = {CanArrivalQuietInMB = function() return true end}
    env.GameObject = {GetGameObjectIdByIndex = function(name, i) return name == 'TppBuddyDog2' and i == 0 and 9728 or 65535 end,
        SendCommand = function(id, cmd) rec('SendCommand', id, cmd.id, cmd.pos); return true end}
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
check(n == 34 and #c.S.keys <= 127, 'thirty-four buddy keys (with the MissionStart copies); within the 1..127 seed index')
local values = hostSeeds(DOG)
check(#values == 40, 'the buddy values (two positions of three and two pairs of gear ammo)')

-- Guest, companion off: the buddy keys are not applied; the guest keeps its horse.
c = Fixture(2, {on = false})
follow(c, values)
check(c.env.vars.buddyType == HORSE and not c.C.own, 'off: the guest loads its own buddy (no seed applied)')

-- Guest, companion on: own kept, the host's dog and look in vars before Load.
c = Fixture(2)
follow(c, values)
check(c.calls[#c.calls][1] == 'load' and c.calls[#c.calls][3] == DOG, 'the retail Load sees the host dog')
check(c.env.vars.buddyTypeMissionStart == DOG, 'the sortie copy (ResetVarsMissionStart source) is the host dog')
check(c.env.vars.buddyDogEyeType == 3, 'the host dog look is seeded')
check(c.C.own and c.C.own.buddyType == HORSE and c.C.own.buddyDogEyeType == 1, 'the guest keeps its own buddy vars')
check(c.find('event=companion;role=guest;step=own_kept;why=seed;own=horse;'), 'own kept is logged')
check(c.find('step=seed_apply;kind=dog;own=horse;mission=30010;'), 'seed apply is logged')
-- D8 (b): a save writes the guest's own vars; vars stay the host's afterwards.
c.env.TppSave.SaveVarsToSlot(3)
check(c.saves[1].buddyType == HORSE and c.saves[1].eye == 1 and c.saves[1].start == HORSE, 'the save slot gets the guest own buddy')
check(c.env.vars.buddyType == DOG and c.env.vars.buddyDogEyeType == 3, 'vars are the host values again after the save')
check(c.find('step=save_own;slot=3;own=horse;live=dog;n=1;'), 'the save policy is logged')
-- The save line: the first three, then one per 5 s burst (a checkpoint after the mission start is visible).
c.env.TppSave.SaveVarsToSlot(0); c.env.TppSave.SaveVarsToSlot(1); c.env.TppSave.SaveVarsToSlot(6)
check(c.find('n=3;') and not c.find('n=4;'), 'a burst after the first three is not logged within 5 s')
c.time = c.time + 5000
c.env.TppSave.SaveVarsToSlot(0)
check(c.find('step=save_own;slot=0;own=horse;live=dog;n=5;') and c.saves[5].buddyType == HORSE, 'the next burst after 5 s is logged')
-- D8 (b) and E19 (run d937191f: guards killed the guest copy's held dog: deadBuddyType 0 -> 4, buddyCommonFlag 32 -> 0):
-- the guest's saves keep its own dead-buddy and common flags too; vars stay live after the save; the end of the
-- session restores them. They are never seeded (not follow keys).
c = Fixture(2)
c.env.vars.deadBuddyType, c.env.vars.buddyCommonFlag = 0, 32
follow(c, values)
check(c.C.own and c.C.own.deadBuddyType == 0 and c.C.own.buddyCommonFlag == 32, 'the own snapshot has the dead-buddy and common flags')
c.env.vars.deadBuddyType, c.env.vars.buddyCommonFlag = 4, 0
c.env.TppSave.SaveVarsToSlot(0)
check(c.saves[#c.saves].dead == 0 and c.saves[#c.saves].flag == 32, 'a save after the mirror died writes the guest own flags')
check(c.env.vars.deadBuddyType == 4 and c.env.vars.buddyCommonFlag == 0, 'the live flags are back after the save')
check(c.find('step=save_own;slot=0;own=horse;live=dog;n=1;own_dead=0;live_dead=4;own_flag=32;live_flag=0;'), 'the save line shows both')
c.C.release('test')
check(c.env.vars.deadBuddyType == 0 and c.env.vars.buddyCommonFlag == 32, 'the end of the session restores them')
for _, key in ipairs(c.S.keys) do check(key[1] ~= 'deadBuddyType' and key[1] ~= 'buddyCommonFlag', 'never a follow seed key') end
c = Fixture(2); follow(c, values)
check(c.C.own and c.C.own.buddyType == HORSE and c.C.own.deadBuddyType == nil, 'a copy without those vars still keeps its own buddy')
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
c.W.handlers[48]({9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, c.time)
c.tick(300)
check(c.masks[#c.masks][1] == 'reset', 'no mask while the host has no companion (knock, sonar stay)')
c.W.handlers[48]({10, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0}, c.time)
c.tick(300)
check(c.masks[#c.masks][1] == 'set', 'the mask returns with the host dog')
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
-- The guest's disable byte: every type but the host's and the loaded one (the iDroid swap path).
local disabled = {}
for _, v in ipairs(c.calls) do if v[1] == 'SetDisableBuddyType' then disabled[v[2]] = true end end
check(disabled[HORSE] and disabled[QUIET] and disabled[WALKER] and not disabled[DOG], 'guest disables horse, Quiet, walker')
check(c.find('step=guard;path=idroid_swap;via=disable_type;allowed=dog;'), 'the byte guard is logged')
-- Reconcile: the host changes to D-Horse; after 3 s the guest calls a horse (original, no refusal).
c.W.handlers[48]({2, 0, 2, 0, 1, 10000, 0, 20000, 0, 0, 0, 0}, c.time)
c.tick(500)
c.time = c.time + 3500
c.W.handlers[48]({3, 0, 2, 0, 1, 10000, 0, 20000, 0, 0, 0, 0}, c.time)
c.tick(200)
check(not c.find('step=reconcile;'), 'no reconcile before 8 s of mismatch (a host warp drops its kind for seconds)')
c.time = c.time + 5000
c.W.handlers[48]({3, 0, 2, 0, 1, 10000, 0, 20000, 0, 0, 0, 0}, c.time)
c.tick(200)
check(c.active == HORSE and c.find('step=reconcile;via=call;kind=horse;have=dog;ok=1;'), 'reconcile by CallBuddy to the host kind')
local unset = false
for _, v in ipairs(c.calls) do if v[1] == 'UnsetDisableBuddyType' and v[2] == HORSE then unset = true end end
check(unset and not c.C.mine[HORSE], 'the new host kind is enabled again before the reconcile')
for _, f in ipairs(c.W.stopHooks) do f(true) end
check(c.C.mine == nil, 'mission end clears the guest bits')
-- The host's Quiet never comes to a guest who plays her.
c.W.handlers[48]({4, 0, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0}, c.time)
c.time = c.time + 20000
c.W.handlers[48]({5, 0, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0}, c.time)
c.tick(200)
check(c.active == HORSE and c.find('step=reconcile_skip;reason=self_quiet;'), 'no buddy Quiet beside the guest Quiet')
-- Without the host's pose (M1 sends kind only) the guest calls the host's kind 4 m in front of its player
-- (playerPos 1,2,3, rotY 0: z + 4), where its camera sees it arrive.
do
    local g = Fixture(2)
    follow(g, values)
    g.active = DOG
    g.tick()
    g.W.handlers[48]({1, 0, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0}, g.time)
    g.tick(200)
    g.time = g.time + 8500
    g.W.handlers[48]({2, 0, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0}, g.time)
    g.tick(200)
    local p
    for _, v in ipairs(g.calls) do if v[1] == 'CallBuddy' and v[2] == HORSE then p = v[3] end end
    check(p and p.x == 1 and p.y == 2 and p.z == 7, 'the reconcile without a host pose calls 4 m in front of the guest')
end
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
check(c.env.vars.buddyTypeMissionStart == NONE, 'the sortie copy is rewritten too')
check(c.find('step=quiet_lock;via=sortie_rewrite;guest_char=quiet;') and c.find('step=seed;kind=none;live=none;mission=30010;'),
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
check(c.find('event=companion;role=host;step=mbquiet_hidden;stock=true;'), 'mbquiet_hidden logged with the stock answer (she would be there)')
-- The host sends its kind.
local kind
for _, t in ipairs(c.tx) do if t.op == 48 then kind = t.v[3] end end
check(kind == 0, 'COMPANION carries kind none after the lock')
-- A short unpair (a mission change) keeps the lock; 10 s unpaired lifts it.
c.W.paired = false
for _, f in ipairs(c.W.unpairedHooks) do f(1) end
c.tick(200)
check(c.called('UnsetDisableBuddyType') == 0 and c.C.guestQuiet == true, 'the lock outlives a short unpair')
c.time = c.time + 11000
c.tick(200)
check(c.called('UnsetDisableBuddyType') == 1 and c.find('step=quiet_unlock;'), 'unlocked after 10 s unpaired')
-- Late join: Quiet out on the host and in its sortie copy (run b993af09 read buddyTypeMissionStart 3 at Mother Base
-- after the heli delete): she is sent away and the sortie copy rewritten too, so a restart cannot bring her back;
-- the host's saves keep its Quiet.
c = Fixture(1, {buddyType = QUIET, active = QUIET})
c.W.handlers[56]({1, 6, 1})
c.tick(6000)
check(c.called('DeleteBuddy') == 1 and c.env.vars.buddyType == NONE and c.env.vars.buddyTypeMissionStart == NONE and
    c.find('step=quiet_lock;via=start_rewrite;guest_char=quiet;'), 'delete_buddy and the sortie copy rewritten')
c.env.TppSave.SaveVarsToSlot(0)
check(c.saves[1].buddyType == QUIET and c.saves[1].start == QUIET, 'the host save keeps Quiet in both')
c.env.vars.buddyTypeMissionStart = QUIET
c.tick(1000)
check(c.env.vars.buddyTypeMissionStart == QUIET, 'the start rewrite waits 5 s between tries')
c.tick(5000)
check(c.env.vars.buddyTypeMissionStart == NONE, 'and rewrites a sortie copy written again')
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
check(ok and msg:find('seed_buddy;kind=walker;vars_type=4;checkpoint=1;', 1, true) and c.C.own.buddyType == WALKER and
    c.C.own.buddyTypeMissionStart == WALKER and c.env.vars.buddyTypeMissionStart == WALKER, 'seed lever (live and sortie copy)')
c.W.mode = 2
check(not c.C.seedLever('horse'), 'the seed lever is lab only')
-- Lab framing lever (host): the loaded companion 5 m in front of the host (playerPos 1,2,3, rotY 0: z + 5); a dog is
-- also warped there (SetWarpPosition on TppBuddyDog2 index 0); with a kind it swaps to that kind (CallBuddy).
c = Fixture(1, {buddyType = DOG, active = DOG})
local A = {}
ok, msg = c.C.lever({'companion_lab', 'call_here'}, 1, A, 30010)
local call, warp
for _, v in ipairs(c.calls) do
    if v[1] == 'CallBuddy' then call = v end
    if v[1] == 'SendCommand' and v[3] == 'SetWarpPosition' then warp = v end
end
check(ok and msg == 'call_here;kind=dog;result=true;warp=1' and call and call[2] == DOG and call[3].x == 1 and call[3].z == 8,
    'call_here: the dog 5 m in front of the host')
check(warp and warp[2] == 9728 and warp[4].x == 1 and warp[4].z == 8, 'call_here warps the dog there')
ok, msg = c.C.lever({'companion_lab', 'call_here', 'horse'}, 2, A, 30010)
check(ok and msg == 'call_here;kind=horse;result=true;warp=0' and c.active == HORSE, 'call_here <kind> swaps the host companion')
check(not c.C.lever({'companion_lab', 'call_here', 'bogus'}, 3, A, 30010), 'call_here refuses an unknown kind')
c.W.mode = 2
check(not c.C.lever({'companion_lab', 'call_here'}, 4, A, 30010), 'call_here is lab only')
c = Fixture(2, {active = DOG})
check(not c.C.lever({'companion_lab', 'call_here'}, 1, A, 30010), 'call_here is host only')
-- M2 (docs/vehicles-companions-design.md 6.2): the host's reliable COMPANION carries the dog's last streamed pose
-- (world_buddy.lua's COMPANION_POSE sample), so a late join or a reconcile calls the dog where the host's is.
c = Fixture(1, {buddyType = DOG, active = DOG})
c.W.buddy = {s = {sent = {k = 1, present = 1, x = 828.5, y = 338.25, z = -1630.75, yaw = 1.5}}}
c.tick()
local pose
for _, t in ipairs(c.tx) do if t.op == 48 then pose = t.v end end
check(pose and pose[3] == 1 and pose[5] == 1 and pose[6] == 828500 and pose[7] == 338250 and pose[8] == -1630750 and pose[9] == 1500,
    'COMPANION: kind dog, present, the streamed pose in mm and the yaw in mrad')
c = Fixture(1, {buddyType = HORSE, active = HORSE})
c.W.buddy = {s = {sent = {k = 1, present = 1, x = 1, y = 2, z = 3, yaw = 0}}}
c.tick()
for _, t in ipairs(c.tx) do if t.op == 48 then pose = t.v end end
check(pose and pose[3] == 2 and pose[5] == 0 and pose[6] == 0, 'another kind than the streamed one: no pose (present 0)')
-- The E19 protection lever on the guest (lab): world_buddy.lua's B.protect.
c = Fixture(2, {active = DOG})
local protected
c.W.buddy = {protect = function(on) protected = on; return true end}
ok, msg = c.C.lever({'companion_lab', 'protect'}, 1, A, 30010)
check(ok and protected == true and msg == 'protect;on=1;result=true', 'companion_lab protect turns the guest copy protection on')
ok, msg = c.C.lever({'companion_lab', 'protect', 'off'}, 2, A, 30010)
check(ok and protected == false and msg == 'protect;on=0;result=true', 'and off')
c.W.mode = 2
check(not c.C.lever({'companion_lab', 'protect'}, 3, A, 30010), 'protect is lab only')
c = Fixture(1, {active = DOG})
check(not c.C.lever({'companion_lab', 'protect'}, 1, A, 30010), 'protect is guest only')
-- The proof's leg marker (lab, either role): world_buddy.lua B.mark.
c = Fixture(2, {active = DOG})
local marked
c.W.buddy = {mark = function(l) marked = l; return true end}
ok, msg = c.C.lever({'companion_lab', 'mark', 'walk'}, 1, A, 30010)
check(ok and marked == 'walk' and msg == 'mark;leg=walk', 'companion_lab mark names the next track windows')
check(not c.C.lever({'companion_lab', 'mark', 'Bad-Label'}, 2, A, 30010), 'mark refuses a label outside [a-z0-9_]')
c.W.mode = 2
check(not c.C.lever({'companion_lab', 'mark', 'run'}, 3, A, 30010), 'mark is lab only')
-- The hold mode lever (lab, guest): world_buddy.lua B.setHold.
c = Fixture(2, {active = DOG})
local held
c.W.buddy = {setHold = function(m) held = m; return m == 'warp' end}
ok, msg = c.C.lever({'companion_lab', 'hold', 'warp'}, 1, A, 30010)
check(ok and held == 'warp' and msg == 'hold;mode=warp', 'companion_lab hold sets the guest hold mode')
check(not c.C.lever({'companion_lab', 'hold', 'bogus'}, 2, A, 30010), 'an unknown mode is refused')
c = Fixture(1, {active = DOG})
check(not c.C.lever({'companion_lab', 'hold', 'warp'}, 1, A, 30010), 'hold is guest only')
-- The near-guard lever (lab, guest): world_buddy.lua B.nearGuard.
c = Fixture(2, {active = DOG})
local near
c.W.buddy = {nearGuard = function(on) near = on; return true end}
ok, msg = c.C.lever({'companion_lab', 'near_guard', 'on'}, 1, A, 30010)
check(ok and near == true and msg == 'near_guard;on=1', 'companion_lab near_guard on')
ok, msg = c.C.lever({'companion_lab', 'near_guard', 'off'}, 2, A, 30010)
check(ok and near == false and msg == 'near_guard;on=0', 'companion_lab near_guard off')
ok, msg = c.C.lever({'companion_lab', 'near_guard', 'shield'}, 3, A, 30010)
check(ok and near == 'shield' and msg == 'near_guard;on=shield', 'companion_lab near_guard shield (E19: the dog in the line of fire)')
check(not c.C.lever({'companion_lab', 'near_guard', 'bogus'}, 4, A, 30010), 'an unknown near_guard form is refused')
c = Fixture(1, {active = DOG})
check(not c.C.lever({'companion_lab', 'near_guard', 'on'}, 1, A, 30010), 'near_guard is guest only')
-- The D7 repair candidate (lab, guest): world_buddy.lua B.repair.
c = Fixture(2, {active = DOG})
local rep
c.W.buddy = {repair = function(on) rep = on; return true end}
ok, msg = c.C.lever({'companion_lab', 'repair', 'on'}, 1, A, 30010)
check(ok and rep == true and msg == 'repair;on=1', 'companion_lab repair on')
ok, msg = c.C.lever({'companion_lab', 'repair', 'off'}, 2, A, 30010)
check(ok and rep == false and msg == 'repair;on=0', 'companion_lab repair off')
c = Fixture(1, {active = DOG})
check(not c.C.lever({'companion_lab', 'repair', 'on'}, 1, A, 30010), 'repair is guest only')
print('world_companion_lua_check: ' .. checks .. ' checks passed')
